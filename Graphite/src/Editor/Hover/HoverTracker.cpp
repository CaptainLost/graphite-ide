#include "Editor/Hover/HoverTracker.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr double kMillisecondsPerSecond = 1000.0;

		void WakeNoLaterThan(HoverActions& actions, double wakeTime)
		{
			actions.WakeAt = actions.WakeAt ? std::min(*actions.WakeAt, wakeTime) : wakeTime;
		}

		bool RetryIsDue(double now, bool wanted, bool asked, bool failed, bool arrived,
						bool& alreadyRetried, std::optional<double>& failedAt, HoverActions& actions)
		{
			if (!wanted || !asked || !failed || arrived || alreadyRetried)
			{
				return false;
			}

			if (!failedAt)
			{
				failedAt = now;
			}

			const double retryAt = *failedAt + HoverTracker::kHoverRetrySeconds;

			if (now >= retryAt)
			{
				alreadyRetried = true;
				return true;
			}

			WakeNoLaterThan(actions, retryAt);
			return false;
		}

		void NoteFirstAsk(bool asked, double now, std::optional<double>& firstAskedAt)
		{
			if (asked && !firstAskedAt)
			{
				firstAskedAt = now;
			}
		}

		bool LoadingIsDue(double now, const std::optional<double>& firstAskedAt, HoverActions& actions)
		{
			if (!firstAskedAt)
			{
				return false;
			}

			const double dueAt = *firstAskedAt + HoverTracker::kLoadingAfterSeconds;

			if (now >= dueAt)
			{
				return true;
			}

			WakeNoLaterThan(actions, dueAt);
			return false;
		}
	}

	void HoverTracker::Restart(const HoverInput& input, HoverActions& actions)
	{
		actions.Cancel = actions.Cancel || m_AskedInfo || m_AskedPreview;
		m_Token = input.Token;
		m_Revision = input.Revision;
		m_RestingSince = input.Now;
		m_AskedInfo = false;
		m_AskedPreview = false;
		m_RetriedInfo = false;
		m_RetriedPreview = false;
		m_InfoFailedAt.reset();
		m_PreviewFailedAt.reset();
		m_InfoFirstAskedAt.reset();
		m_PreviewFirstAskedAt.reset();
		m_Latched = false;
		m_LeftAt.reset();
	}

	HoverActions HoverTracker::Update(const HoverInput& input)
	{
		HoverActions actions;
		const HoverSettings& settings = input.Settings;

		if (input.InputHappened)
		{
			Restart(input, actions);
			m_Token.reset();
			m_Dismissed = true;
		}
		else if (m_Dismissed && input.MouseMoved)
		{
			m_Dismissed = false;
		}
		if (m_Dismissed)
		{
			m_WasVisible = false;
			return actions;
		}

		if (input.Revision != m_Revision)
		{
			Restart(input, actions);
		}
		else if (input.MouseInTooltip)
		{
			m_Latched = true;
			m_LeftAt.reset();
		}
		else if (input.Token == m_Token)
		{
			m_Latched = false;
			m_LeftAt.reset();
		}
		else if (input.Token || !m_WasVisible)
		{
			Restart(input, actions);
		}
		else
		{
			if (!m_LeftAt)
			{
				m_LeftAt = input.Now;
			}
			if (input.Now >= *m_LeftAt + kLeaveGraceSeconds)
			{
				Restart(input, actions);
			}
		}
		const bool held = m_Latched || m_LeftAt.has_value();

		const bool ctrl = held ? m_HeldCtrl : input.Ctrl;
		const bool previewOnlyWithCtrl = settings.Preview == DefinitionPreviewMode::WithCtrl;
		const bool wantInfo = settings.ShowInfo && (!previewOnlyWithCtrl || !ctrl);
		const bool wantPreview = settings.Preview == DefinitionPreviewMode::Always || (previewOnlyWithCtrl && ctrl);

		const double due = m_RestingSince + settings.DelayMs / kMillisecondsPerSecond;
		const bool elapsed = m_Token.has_value() && input.Now >= due;
		if (m_Token && !elapsed && (wantInfo || wantPreview))
		{
			actions.WakeAt = due;
		}
		if (m_LeftAt)
		{
			const double graceEnd = *m_LeftAt + kLeaveGraceSeconds;
			WakeNoLaterThan(actions, graceEnd);
		}

		if (elapsed && !m_LeftAt)
		{
			const bool retryInfo = RetryIsDue(input.Now, wantInfo, m_AskedInfo, input.InfoFailed, input.InfoArrived,
											  m_RetriedInfo, m_InfoFailedAt, actions);
			const bool retryPreview = RetryIsDue(input.Now, wantPreview, m_AskedPreview, input.PreviewFailed,
												 input.PreviewArrived, m_RetriedPreview, m_PreviewFailedAt, actions);

			actions.AskInfo = (wantInfo && !m_AskedInfo && !input.InfoArrived) || retryInfo;
			actions.AskPreview = (wantPreview && !m_AskedPreview && !input.PreviewArrived) || retryPreview;
			m_AskedInfo = m_AskedInfo || actions.AskInfo;
			m_AskedPreview = m_AskedPreview || actions.AskPreview;

			NoteFirstAsk(actions.AskInfo, input.Now, m_InfoFirstAskedAt);
			NoteFirstAsk(actions.AskPreview, input.Now, m_PreviewFirstAskedAt);
		}

		actions.ShowInfo = elapsed && wantInfo && input.InfoArrived;
		actions.ShowPreview = elapsed && wantPreview && input.PreviewArrived;

		const bool awaitedHere = elapsed && !m_LeftAt;
		const bool infoAwaited = awaitedHere && wantInfo && m_AskedInfo && !input.InfoArrived && !input.InfoFailed;
		const bool previewAwaited =
			awaitedHere && wantPreview && m_AskedPreview && !input.PreviewArrived && !input.PreviewFailed;

		actions.InfoLoading = infoAwaited && LoadingIsDue(input.Now, m_InfoFirstAskedAt, actions);
		actions.PreviewLoading = previewAwaited && LoadingIsDue(input.Now, m_PreviewFirstAskedAt, actions);

		actions.InfoGaveUp =
			elapsed && wantInfo && input.InfoFailed && input.InfoFailureSaysWhy && m_RetriedInfo && !actions.AskInfo;
		actions.PreviewGaveUp = elapsed && wantPreview && input.PreviewFailed && input.PreviewFailureSaysWhy &&
								m_RetriedPreview && !actions.AskPreview;

		actions.PreviewHeldByCtrl = previewOnlyWithCtrl && ctrl;
		actions.ShowDiagnostic = held ? m_HeldDiagnostic : input.DiagnosticUnderMouse;

		if (!held)
		{
			m_HeldCtrl = input.Ctrl;
			m_HeldDiagnostic = input.DiagnosticUnderMouse;
		}
		m_WasVisible = actions.Visible();
		return actions;
	}
}

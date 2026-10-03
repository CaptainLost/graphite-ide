#include "Editor/Hover/HoverTracker.h"

#include <algorithm>

namespace Graphite {

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
		m_Latched = false;
		m_LeftAt.reset();
	}

	HoverActions HoverTracker::Update(const HoverInput& input)
	{
		HoverActions actions;
		const HoverSettings& settings = input.Settings;

		// Rule 2: input dismisses the tooltip until the mouse moves. Waiting out the delay instead
		// would pop it back up at every pause in typing, and a token change is no sign of intent
		// either, since an edit shifts offsets under a still mouse.
		if (input.InputHappened)
		{
			Restart(input, actions);
			m_Token.reset();
			m_Dismissed = true;
		}
		else if (m_Dismissed && input.MouseMoved)
		{
			m_Dismissed = false; // the token change below starts the delay afresh
		}
		if (m_Dismissed)
		{
			m_WasVisible = false;
			return actions;
		}

		// Rules 2 and 5: a new version of the text makes every answer stale. The mouse inside the
		// tooltip holds it as it is. Back on the token, Ctrl swaps again. Another token closes.
		// Leaving both closes only after a grace, so a diagonal path can still reach the tooltip;
		// with nothing on screen there is nothing to reach and it closes at once.
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

		// Rule 1: what the settings ask for. While held Ctrl no longer swaps the content, so the
		// Ctrl state from before the hold stands.
		const bool ctrl = held ? m_HeldCtrl : input.Ctrl;
		const bool wantInfo =
			settings.ShowInfo && (settings.Preview != DefinitionPreviewMode::WithCtrl || !ctrl);
		const bool wantPreview = settings.Preview == DefinitionPreviewMode::Always || (settings.Preview == DefinitionPreviewMode::WithCtrl && ctrl);

		// Rule 3: nothing is asked until the mouse has rested for the delay; until then the
		// frame loop is told when to come back, since a resting mouse produces no events. The
		// grace's end needs a frame too. Rule 6: with nothing wanted there is nothing to wait for.
		const double due = m_RestingSince + settings.DelayMs / 1000.0;
		const bool elapsed = m_Token.has_value() && input.Now >= due;
		if (m_Token && !elapsed && (wantInfo || wantPreview))
		{
			actions.WakeAt = due;
		}
		if (m_LeftAt)
		{
			const double graceEnd = *m_LeftAt + kLeaveGraceSeconds;
			actions.WakeAt = actions.WakeAt ? std::min(*actions.WakeAt, graceEnd) : graceEnd;
		}

		// An answer already held for this token and text is never asked for again, and a Ctrl
		// swap after the delay asks for its part at once. A mouse in transit asks nothing.
		if (elapsed && !m_LeftAt)
		{
			// A question asked and failed is asked once more after a pause, never twice. Read
			// before this frame's own asks: a question asked only now has not failed yet, and the
			// widget clears the old failure as it asks.
			const auto retry = [&](bool want, bool asked, bool failed, bool arrived,
								   bool& retried, std::optional<double>& failedAt) {
				if (!want || !asked || !failed || arrived || retried)
				{
					return false;
				}
				if (!failedAt)
				{
					failedAt = input.Now;
				}
				const double retryAt = *failedAt + kRetrySeconds;
				if (input.Now >= retryAt)
				{
					retried = true;
					return true;
				}
				actions.WakeAt = actions.WakeAt ? std::min(*actions.WakeAt, retryAt) : retryAt;
				return false;
			};

			const bool retryInfo = retry(wantInfo, m_AskedInfo, input.InfoFailed,
										 input.InfoArrived, m_RetriedInfo, m_InfoFailedAt);
			const bool retryPreview =
				retry(wantPreview, m_AskedPreview, input.PreviewFailed, input.PreviewArrived,
					  m_RetriedPreview, m_PreviewFailedAt);

			actions.AskInfo = (wantInfo && !m_AskedInfo && !input.InfoArrived) || retryInfo;
			actions.AskPreview =
				(wantPreview && !m_AskedPreview && !input.PreviewArrived) || retryPreview;
			m_AskedInfo = m_AskedInfo || actions.AskInfo;
			m_AskedPreview = m_AskedPreview || actions.AskPreview;
		}

		// Rule 4: the parts are shown once the delay has passed and their answer is in. A
		// diagnostic needs no question and so no delay; while held it stays, since the mouse is
		// no longer over the text that carries it.
		actions.ShowInfo = elapsed && wantInfo && input.InfoArrived;
		actions.ShowPreview = elapsed && wantPreview && input.PreviewArrived;
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

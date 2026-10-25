#pragma once

#include "Editor/EditorSettings.h"

#include <cstddef>
#include <optional>

namespace Graphite {

	struct HoverToken
	{
		std::size_t Line = 0;
		std::size_t From = 0;
		std::size_t To = 0;

		friend bool operator==(const HoverToken&, const HoverToken&) = default;
	};

	struct HoverInput
	{
		std::optional<HoverToken> Token;
		bool MouseInTooltip = false;
		bool MouseMoved = false;
		bool Ctrl = false;
		bool DiagnosticUnderMouse = false;
		bool InputHappened = false;
		std::size_t Revision = 0;
		HoverSettings Settings;
		double Now = 0.0;

		// WARNING: Arrival is judged against the tracker's own Current() token as of the previous
		// Update, not against Token: during the leave grace the two differ.
		bool InfoArrived = false;
		bool PreviewArrived = false;

		bool InfoFailed = false;
		bool PreviewFailed = false;
	};

	struct HoverActions
	{
		bool AskInfo = false;
		bool AskPreview = false;
		bool Cancel = false;

		bool ShowDiagnostic = false;
		bool ShowInfo = false;
		bool ShowPreview = false;

		bool InfoLoading = false;
		bool PreviewLoading = false;

		bool InfoGaveUp = false;
		bool PreviewGaveUp = false;

		std::optional<double> WakeAt;

		bool Visible() const
		{
			return ShowDiagnostic || ShowInfo || ShowPreview || InfoLoading || PreviewLoading || InfoGaveUp ||
				   PreviewGaveUp;
		}
	};

	class HoverTracker
	{
	public:
		static constexpr double kLeaveGraceSeconds = 0.25;
		static constexpr double kHoverRetrySeconds = 0.5;
		static constexpr double kLoadingAfterSeconds = 0.2;

		HoverActions Update(const HoverInput& input);

		std::optional<HoverToken> Current() const { return m_Token; }

	private:
		void Restart(const HoverInput& input, HoverActions& actions);

		std::optional<HoverToken> m_Token;
		std::size_t m_Revision = 0;
		double m_RestingSince = 0.0;
		bool m_AskedInfo = false;
		bool m_AskedPreview = false;
		bool m_RetriedInfo = false;
		bool m_RetriedPreview = false;
		std::optional<double> m_InfoFailedAt;
		std::optional<double> m_PreviewFailedAt;
		std::optional<double> m_InfoFirstAskedAt;
		std::optional<double> m_PreviewFirstAskedAt;
		bool m_WasVisible = false;
		bool m_Dismissed = false;
		bool m_Latched = false;
		std::optional<double> m_LeftAt;
		bool m_HeldCtrl = false;
		bool m_HeldDiagnostic = false;
	};
}

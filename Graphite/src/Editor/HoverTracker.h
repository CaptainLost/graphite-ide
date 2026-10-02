#pragma once

#include "Editor/EditorSettings.h"

#include <cstddef>
#include <optional>

namespace Graphite {

	// The token under the mouse, in the document's own terms. Equal tokens are "the same place".
	struct HoverToken
	{
		std::size_t Line = 0;
		std::size_t From = 0; // raw byte offsets in the line
		std::size_t To = 0;

		friend bool operator==(const HoverToken&, const HoverToken&) = default;
	};

	struct HoverInput
	{
		std::optional<HoverToken> Token;   // null over anything that is not an identifier
		bool MouseInTooltip = false;       // read from the tooltip's previous-frame rectangle
		bool MouseMoved = false;           // the pointer position changed since the last frame
		bool Ctrl = false;
		bool DiagnosticUnderMouse = false;
		// A key press other than a lone modifier, a click, a scroll of the editor surface or an
		// edit, this frame. Ctrl, and the wheel over the tooltip, are not input: the first swaps
		// the content and the second scrolls the preview.
		bool InputHappened = false;
		std::size_t Revision = 0;          // the document's buffer revision
		HoverSettings Settings;
		double Now = 0.0;                  // seconds

		// Whether the answer of each kind has arrived for the tracker's own token — `Current()`
		// as of the previous Update, not `Token` — and `Revision`. During the leave grace and
		// while the mouse is in the tooltip the two differ, and the answers shown belong to
		// `Current()`. The widget reads these off the document's hover state.
		bool InfoArrived = false;
		bool PreviewArrived = false;
	};

	struct HoverActions
	{
		bool AskInfo = false;
		bool AskPreview = false;
		bool Cancel = false;               // cancel outstanding questions; the token was left

		bool ShowDiagnostic = false;
		bool ShowInfo = false;
		bool ShowPreview = false;

		// When the tracker wants to be asked again even if nothing moves; nullopt for "no need".
		std::optional<double> WakeAt;

		bool Visible() const { return ShowDiagnostic || ShowInfo || ShowPreview; }
	};

	// Decides, frame by frame, when a hover asks the language server, what the tooltip shows and
	// when it closes. It holds no clock of its own — the caller passes `Now` — so the timing is
	// testable without waiting.
	class HoverTracker
	{
	public:
		// How long the tooltip waits for a mouse that has left the token on its way to the
		// tooltip. A diagonal path crosses text that is neither, and closing on the first such
		// frame would make the tooltip impossible to reach.
		static constexpr double kLeaveGraceSeconds = 0.25;

		HoverActions Update(const HoverInput& input);

		// The token and revision the current answers belong to; the widget keys what it shows on
		// it.
		std::optional<HoverToken> Current() const { return m_Token; }

	private:
		// Drops the token's questions and state and starts over on whatever is under the mouse.
		void Restart(const HoverInput& input, HoverActions& actions);

		std::optional<HoverToken> m_Token;
		std::size_t m_Revision = 0;
		double m_RestingSince = 0.0;
		bool m_AskedInfo = false;
		bool m_AskedPreview = false;
		bool m_WasVisible = false;         // the last frame showed something
		bool m_Dismissed = false;          // input closed the tooltip; only a mouse move reopens it
		// Kept across frames, not read off the input: it survives the leave grace, so a mouse
		// that slips out of the tooltip and back is still latched.
		bool m_Latched = false;            // mouse entered the tooltip; Ctrl no longer swaps it
		std::optional<double> m_LeftAt;    // when the mouse left both token and tooltip
		// While not held (latched or in the grace) these follow each frame's input, so on the
		// frame the hold starts they still say what was true just before.
		bool m_HeldCtrl = false;
		bool m_HeldDiagnostic = false;
	};
}

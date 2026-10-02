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
		bool Ctrl = false;
		bool DiagnosticUnderMouse = false;
		bool InputHappened = false;        // a key press, click, editor scroll or edit this frame
		std::size_t Revision = 0;          // the document's buffer revision
		HoverSettings Settings;
		double Now = 0.0;                  // seconds

		// Whether the answer for (Token, Revision) of each kind has arrived. The widget reads
		// these off the document's hover state; the tracker only needs to know they are there.
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
		HoverActions Update(const HoverInput& input);

		// The token and revision the current answers belong to; the widget keys what it shows on it.
		std::optional<HoverToken> Current() const { return m_Token; }

	private:
		std::optional<HoverToken> m_Token;
		std::size_t m_Revision = 0;
		double m_RestingSince = 0.0;
		bool m_AskedInfo = false;
		bool m_AskedPreview = false;
		bool m_Latched = false;            // mouse entered the tooltip; Ctrl no longer swaps it
		// While unlatched these follow the last frame's input, so on the frame the mouse enters
		// the tooltip they still hold what was true just before it did.
		bool m_LatchedCtrl = false;
		bool m_LatchedDiagnostic = false;
	};
}

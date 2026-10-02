#include "Editor/HoverTracker.h"

namespace Graphite {

	HoverActions HoverTracker::Update(const HoverInput& input)
	{
		HoverActions actions;
		const HoverSettings& settings = input.Settings;

		// Rule 5: the mouse inside the tooltip holds it as it was — a tooltip that changed or
		// vanished under the mouse could not be scrolled or read. Leaving it drops the hold.
		m_Latched = input.MouseInTooltip;

		// Rule 2: any input closes the tooltip; another token, or another version of the text,
		// makes what was asked stale. Each starts the delay over and cancels what is in flight.
		const bool closing = input.InputHappened;
		const bool edited = input.Revision != m_Revision;
		const bool moved = !m_Latched && input.Token != m_Token;
		if (closing || edited || moved)
		{
			actions.Cancel = m_AskedInfo || m_AskedPreview;
			m_Token = input.Token;
			m_Revision = input.Revision;
			m_RestingSince = input.Now;
			m_AskedInfo = false;
			m_AskedPreview = false;
			if (closing || edited)
				m_Latched = false;
		}

		// Rule 1: what the settings ask for. Inside the tooltip Ctrl no longer swaps it, so the
		// Ctrl state from before the mouse entered stands.
		const bool ctrl = m_Latched ? m_LatchedCtrl : input.Ctrl;
		const bool wantInfo = settings.ShowInfo && (settings.Preview != DefinitionPreviewMode::WithCtrl || !ctrl);
		const bool wantPreview = settings.Preview == DefinitionPreviewMode::Always
			|| (settings.Preview == DefinitionPreviewMode::WithCtrl && ctrl);

		// Rule 3: nothing is asked until the mouse has rested for the delay; until then the
		// frame loop is told when to come back, since a resting mouse produces no events. Rule 6:
		// with nothing wanted there is nothing to wait for.
		const double due = m_RestingSince + settings.DelayMs / 1000.0;
		const bool elapsed = m_Token.has_value() && input.Now >= due;
		if (m_Token && !elapsed && (wantInfo || wantPreview))
			actions.WakeAt = due;

		// An answer already held for this token and text is never asked for again, and a Ctrl
		// swap after the delay asks for its part at once.
		if (elapsed && !closing)
		{
			actions.AskInfo = wantInfo && !m_AskedInfo && !input.InfoArrived;
			actions.AskPreview = wantPreview && !m_AskedPreview && !input.PreviewArrived;
			m_AskedInfo = m_AskedInfo || actions.AskInfo;
			m_AskedPreview = m_AskedPreview || actions.AskPreview;
		}

		// Rule 4: the parts are shown once the delay has passed and their answer is in. A
		// diagnostic needs no question and so no delay; while latched it stays, since the mouse
		// is no longer over the text that carries it.
		if (!closing)
		{
			actions.ShowInfo = elapsed && wantInfo && input.InfoArrived;
			actions.ShowPreview = elapsed && wantPreview && input.PreviewArrived;
			actions.ShowDiagnostic = m_Latched ? m_LatchedDiagnostic : input.DiagnosticUnderMouse;
		}

		if (!m_Latched)
		{
			m_LatchedCtrl = input.Ctrl;
			m_LatchedDiagnostic = input.DiagnosticUnderMouse;
		}
		return actions;
	}
}

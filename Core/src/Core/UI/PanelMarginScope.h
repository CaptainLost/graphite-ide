#pragma once

struct ImGuiWindow;

namespace Core {

	// WARNING: Made inside a panel's window and ended before that window is, and only between whole lines. What is
	// laid out inside it stands the interface's window padding in from the window's two sides, where a flush panel's
	// window has none (Panel::Sides::Flush), docked or floating; in a padded window, or in a child window such as a
	// list's, it changes nothing. The window's work rect and content region are narrowed for the scope's life, so a
	// width taken from what is left on the line ends at the margin. A scope must not straddle a group or a table, which
	// put back at their end the indent or the work rect they began with, and scopes must not nest: a scope inside
	// another adds the margin again.
	class PanelMarginScope final
	{
	public:
		PanelMarginScope();
		~PanelMarginScope();

		PanelMarginScope(const PanelMarginScope&) = delete;
		PanelMarginScope& operator=(const PanelMarginScope&) = delete;
		PanelMarginScope(PanelMarginScope&&) = delete;
		PanelMarginScope& operator=(PanelMarginScope&&) = delete;

	private:
		ImGuiWindow* m_Window = nullptr;
		float m_Margin = 0.0f;
	};
}

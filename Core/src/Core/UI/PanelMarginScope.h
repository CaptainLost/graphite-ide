#pragma once

struct ImGuiWindow;

namespace Core {

	// WARNING: Made inside a panel's window and ended before that window is, and only between whole lines. What is
	// laid out inside it stands the interface's window padding in from the window's two sides, where a flush panel's
	// window has none (Panel::Sides::Flush); in a padded window, or in a child window, it changes nothing. The window's
	// work rect and content region are narrowed for the scope's life, so a width taken from what is left on the line
	// ends at the margin.
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

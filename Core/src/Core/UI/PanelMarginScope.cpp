#include "corepch.h"

#include "Core/UI/PanelMarginScope.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace Core {

	namespace {
		// WARNING: A child window has no padding of its own unless it asks for one, and whatever it holds is laid out
		// to its edges on purpose, so only a top-level or docked window is given the margin it lacks.
		float MarginIn(const ImGuiWindow& window)
		{
			const bool isChildWindow = (window.Flags & ImGuiWindowFlags_ChildWindow) != 0;

			return isChildWindow ? 0.0f : std::max(0.0f, ImGui::GetStyle().WindowPadding.x - window.WindowPadding.x);
		}
	}

	PanelMarginScope::PanelMarginScope()
		: m_Window(ImGui::GetCurrentWindow()), m_Margin(MarginIn(*m_Window))
	{
		// WARNING: Indent takes a width of nothing as the style's indent spacing, so a window that needs no margin is
		// left alone.
		if (m_Margin <= 0.0f)
		{
			return;
		}

		ImGui::Indent(m_Margin);
		m_Window->WorkRect.Max.x -= m_Margin;
		m_Window->ContentRegionRect.Max.x -= m_Margin;
	}

	PanelMarginScope::~PanelMarginScope()
	{
		if (m_Margin <= 0.0f)
		{
			return;
		}

		m_Window->ContentRegionRect.Max.x += m_Margin;
		m_Window->WorkRect.Max.x += m_Margin;
		ImGui::Unindent(m_Margin);
	}
}

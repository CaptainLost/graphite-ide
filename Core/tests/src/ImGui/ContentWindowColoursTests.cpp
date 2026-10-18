#include <doctest/doctest.h>

#include "Core/ImGui/ContentWindowColours.h"
#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace {
	class IsolatedImGuiContext
	{
	public:
		IsolatedImGuiContext()
			: m_Context(ImGui::CreateContext())
		{
			ImGui::GetIO().IniFilename = nullptr;
		}

		~IsolatedImGuiContext()
		{
			ImGui::DestroyContext(m_Context);
		}

		IsolatedImGuiContext(const IsolatedImGuiContext&) = delete;
		IsolatedImGuiContext& operator=(const IsolatedImGuiContext&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("Content window colours paint a window, its children and its selected tab in the content colour until popped")
{
	IsolatedImGuiContext context;
	Core::ChromePalette palette = Core::Theme::DefaultPalette();
	palette.Content = Core::Color{0.9f, 0.2f, 0.1f, 1.0f};
	Core::Theme::Apply(palette);

	Core::PushContentWindowColours();
	for (const ImGuiCol slot : {ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_TabSelected, ImGuiCol_TabDimmedSelected})
	{
		CAPTURE(slot);
		CHECK(ImGui::GetStyleColorVec4(slot).x == doctest::Approx(0.9f));
		CHECK(ImGui::GetStyleColorVec4(slot).y == doctest::Approx(0.2f));
	}
	Core::PopContentWindowColours();

	CHECK(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x == doctest::Approx(palette.Background.R));
	CHECK(GImGui->ColorStack.Size == 0);
}

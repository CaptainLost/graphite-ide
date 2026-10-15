#include <doctest/doctest.h>

#include "Core/ImGui/ContextMenu.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

	constexpr ImVec2 kWindowPos(100.0f, 100.0f);
	constexpr ImVec2 kWindowSize(300.0f, 200.0f);
	constexpr const char* kMenuId = "##menu";

	class MenuWindow
	{
	public:
		MenuWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.Fonts->Build();
		}

		~MenuWindow() { ImGui::DestroyContext(m_Context); }

		MenuWindow(const MenuWindow&) = delete;
		MenuWindow& operator=(const MenuWindow&) = delete;

		bool Next(bool clickedHere)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(kWindowPos);
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin("Menu host");

			const bool open = Core::BeginRightClickMenu(kMenuId, clickedHere);

			if (open)
			{
				ImGui::MenuItem("Item");
				m_MenuFlags = ImGui::GetCurrentWindow()->Flags;
				ImGui::EndPopup();
			}

			ImGui::End();
			ImGui::EndFrame();

			return open;
		}

		ImGuiWindowFlags GetMenuFlags() const { return m_MenuFlags; }

	private:
		ImGuiContext* m_Context = nullptr;
		ImGuiWindowFlags m_MenuFlags = ImGuiWindowFlags_None;
	};
}

TEST_CASE("A right-click menu opens when its caller says the click landed, and stays open")
{
	MenuWindow window;

	CHECK_FALSE(window.Next(false));
	CHECK(window.Next(true));
	CHECK(window.Next(false));
}

TEST_CASE("A right-click menu cannot be dragged by its body")
{
	MenuWindow window;

	REQUIRE(window.Next(true));

	CHECK((window.GetMenuFlags() & ImGuiWindowFlags_NoMove) != 0);
}

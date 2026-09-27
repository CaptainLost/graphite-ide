#include <doctest/doctest.h>

#include "Core/ImGui/TabContextMenu.h"

#include <imgui.h>

#include <string>
#include <vector>

using Core::TabCloseAction;
using Core::TabsToClose;
using Names = std::vector<std::string>;

namespace {
	const Names kTabs{"main.cpp", "Settings", "Session", "Foo.h"};

	constexpr ImVec2 kWindowPos(100.0f, 100.0f);
	constexpr ImVec2 kWindowSize(300.0f, 200.0f);

	// A point a few pixels into the title bar, which is where Begin puts the item a floating
	// window's tab is.
	constexpr ImVec2 kOverTitleBar(kWindowPos.x + 50.0f, kWindowPos.y + 5.0f);

	// A context of its own with one floating window at a known place, drawn a frame at a
	// time the way an owner draws it: Begin, then the menu straight after.
	class TabWindow
	{
	public:
		TabWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.Fonts->Build();
		}

		~TabWindow()
		{
			ImGui::DestroyContext(m_Context);
		}

		TabWindow(const TabWindow&) = delete;
		TabWindow& operator=(const TabWindow&) = delete;

		struct Frame
		{
			bool TitleBarHovered = false;
			bool MenuOpen = false;
		};

		// Runs one frame and reports what the window's title bar and menu looked like right
		// after the menu was drawn.
		Frame Next()
		{
			Frame frame;

			ImGui::NewFrame();
			ImGui::SetNextWindowPos(kWindowPos);
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin("Tab");

			Core::DrawTabContextMenu(m_Requests);

			frame.TitleBarHovered = ImGui::IsItemHovered();
			frame.MenuOpen = ImGui::IsPopupOpen("##tabContextMenu");

			ImGui::End();
			ImGui::EndFrame();

			return frame;
		}

	private:
		ImGuiContext* m_Context = nullptr;
		Core::TabCloseRequests m_Requests;
	};
}

TEST_CASE("Close picks only the clicked tab")
{
	CHECK(TabsToClose(kTabs, "Session", TabCloseAction::This) == Names{"Session"});
}

TEST_CASE("Close Others picks every tab but the clicked one, in tab order")
{
	CHECK(TabsToClose(kTabs, "main.cpp", TabCloseAction::Others) == Names{"Settings", "Session", "Foo.h"});
	CHECK(TabsToClose(kTabs, "Session", TabCloseAction::Others) == Names{"main.cpp", "Settings", "Foo.h"});
}

TEST_CASE("Close to the Right picks the tabs after the clicked one")
{
	CHECK(TabsToClose(kTabs, "Settings", TabCloseAction::ToTheRight) == Names{"Session", "Foo.h"});
	CHECK(TabsToClose(kTabs, "main.cpp", TabCloseAction::ToTheRight) == Names{"Settings", "Session", "Foo.h"});
	CHECK(TabsToClose(kTabs, "Foo.h", TabCloseAction::ToTheRight).empty());
}

TEST_CASE("Close All picks every tab")
{
	CHECK(TabsToClose(kTabs, "Settings", TabCloseAction::All) == kTabs);
}

TEST_CASE("A lone tab closes only itself")
{
	const Names alone{"Foo.h"};

	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::This) == alone);
	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::Others).empty());
	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::ToTheRight).empty());
	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::All) == alone);
}

TEST_CASE("A clicked tab missing from the list closes only itself, and only through Close")
{
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::This) == Names{"Gone"});
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::Others).empty());
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::ToTheRight).empty());
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::All).empty());
}

TEST_CASE("Drawing the menu leaves the title bar reported as hovered")
{
	// An owner asks about the tab after the menu too - a preview document promotes itself on
	// a double click - so reading the window must not stop ImGui answering for its tab.
	TabWindow window;
	window.Next();

	ImGui::GetIO().AddMousePosEvent(kOverTitleBar.x, kOverTitleBar.y);
	window.Next();

	CHECK(window.Next().TitleBarHovered);
}

TEST_CASE("A right click on the title bar opens the menu on release")
{
	TabWindow window;
	window.Next();

	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(kOverTitleBar.x, kOverTitleBar.y);
	REQUIRE_FALSE(window.Next().MenuOpen);

	io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
	REQUIRE_FALSE(window.Next().MenuOpen);

	io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
	window.Next();

	CHECK(window.Next().MenuOpen);
}

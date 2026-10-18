#include <doctest/doctest.h>

#include "Core/ImGui/DockSpaceHost.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <string>
#include <vector>

using Core::DockLayout;
using Core::DockSpaceHost;

namespace {
	class DockingContext
	{
	public:
		DockingContext()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& imGuiIo = ImGui::GetIO();
			imGuiIo.DisplaySize = ImVec2(800.0f, 600.0f);
			imGuiIo.DeltaTime = 1.0f / 60.0f;
			imGuiIo.IniFilename = nullptr;
			imGuiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			imGuiIo.Fonts->Build();
		}

		~DockingContext() { ImGui::DestroyContext(m_Context); }

		DockingContext(const DockingContext&) = delete;
		DockingContext& operator=(const DockingContext&) = delete;

		void Frame(DockSpaceHost& host, const DockLayout& layout, const std::function<void()>& insideHost = {},
				   const std::function<void()>& besideDockSpace = {})
		{
			ImGui::NewFrame();

			host.Begin(layout, {}, besideDockSpace);
			if (insideHost)
			{
				insideHost();
			}
			host.End();

			for (const std::vector<std::string>* panelNames : {&layout.Left, &layout.Bottom, &layout.Center})
			{
				for (const std::string& panelName : *panelNames)
				{
					ImGui::Begin(panelName.c_str());
					ImGui::End();
				}
			}

			ImGui::EndFrame();
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	constexpr int kSettleFrames = 3;

	DockLayout LayoutWithPanelBesideCenter()
	{
		DockLayout layout;
		layout.Left = {"Explorer"};
		return layout;
	}
}

TEST_CASE("The central node counts as empty while no window is docked in it")
{
	DockingContext context;
	DockSpaceHost host;
	const DockLayout layout = LayoutWithPanelBesideCenter();

	bool empty = false;
	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout, [&] { empty = host.IsCenterEmpty(); });
	}

	CHECK(empty);
}

TEST_CASE("The central node stops counting as empty once a window is docked in it")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout = LayoutWithPanelBesideCenter();
	layout.Center = {"Document"};

	bool empty = true;
	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout, [&] { empty = host.IsCenterEmpty(); });
	}

	CHECK_FALSE(empty);
}

TEST_CASE("CenterEmptyState claims exactly the central node, in the window that paints its background")
{
	DockingContext context;
	DockSpaceHost host;
	const DockLayout layout = LayoutWithPanelBesideCenter();

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout);
	}

	ImVec2 claimedMax;
	ImVec2 nodeMax;
	int vertexCountAfter = 0;
	int vertexCountBefore = 0;

	context.Frame(host, layout, [&] {
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetCenterNodeId());
		REQUIRE(node != nullptr);
		REQUIRE(node->HostWindow != nullptr);
		REQUIRE(node->HostWindow != ImGui::GetCurrentWindow());

		vertexCountBefore = node->HostWindow->DrawList->VtxBuffer.Size;

		CHECK_FALSE(host.CenterEmptyState("Nothing open.", "Open"));

		vertexCountAfter = node->HostWindow->DrawList->VtxBuffer.Size;
		claimedMax = node->HostWindow->DC.CursorMaxPos;
		nodeMax = ImVec2(node->Pos.x + node->Size.x, node->Pos.y + node->Size.y);
	});

	CHECK(vertexCountAfter > vertexCountBefore);
	CHECK(claimedMax.x == doctest::Approx(nodeMax.x));
	CHECK(claimedMax.y == doctest::Approx(nodeMax.y));
}

TEST_CASE("CenterEmptyState submits nothing while a window is docked in the central node")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout = LayoutWithPanelBesideCenter();
	layout.Center = {"Document"};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout);
	}

	context.Frame(host, layout, [&] {
		const ImGuiID itemIdBefore = ImGui::GetItemID();
		const ImVec2 cursorBefore = ImGui::GetCursorScreenPos();

		CHECK_FALSE(host.CenterEmptyState("Nothing open.", "Open"));

		CHECK(ImGui::GetItemID() == itemIdBefore);
		CHECK(ImGui::GetCursorScreenPos().x == doctest::Approx(cursorBefore.x));
		CHECK(ImGui::GetCursorScreenPos().y == doctest::Approx(cursorBefore.y));
	});
}

TEST_CASE("DockSpaceHost draws what it is given above the dockspace and gives the dockspace the rest")
{
	DockingContext context;
	DockSpaceHost host;
	bool drawn = false;
	float bannerBottom = 0.0f;
	constexpr float kBannerWidth = 10.0f;
	constexpr float kBannerHeight = 24.0f;
	constexpr float kPixelTolerance = 1.0f;
	const DockLayout emptyLayout;

	const auto banner = [&] {
		drawn = true;
		ImGui::Dummy(ImVec2(kBannerWidth, kBannerHeight));
		bannerBottom = ImGui::GetCursorScreenPos().y;
	};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		ImGui::NewFrame();
		host.Begin(emptyLayout, banner);
		host.End();
		ImGui::EndFrame();
	}

	const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetCenterNodeId());

	CHECK(drawn);
	REQUIRE(node != nullptr);
	CHECK(node->Pos.y >= bannerBottom - kPixelTolerance);
}

namespace {
	ImVec2 TabCenter(const char* name)
	{
		const ImGuiWindow* window = ImGui::FindWindowByName(name);
		REQUIRE(window != nullptr);

		const ImRect& tab = window->DC.DockTabItemRect;
		return ImVec2((tab.Min.x + tab.Max.x) * 0.5f, (tab.Min.y + tab.Max.y) * 0.5f);
	}

	ImGuiID SelectedTab(const DockSpaceHost& host)
	{
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetCenterNodeId());
		REQUIRE(node != nullptr);
		REQUIRE(node->TabBar != nullptr);

		return node->TabBar->SelectedTabId;
	}
}

TEST_CASE("A right click on a tab that is not selected leaves the selection where it was")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout = LayoutWithPanelBesideCenter();
	layout.Center = {"First", "Second"};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout);
	}

	const ImGuiID selectedTab = SelectedTab(host);
	const char* unselectedTabName = selectedTab == ImGui::FindWindowByName("First")->TabId ? "Second" : "First";

	ImGuiIO& imGuiIo = ImGui::GetIO();
	imGuiIo.AddMousePosEvent(TabCenter(unselectedTabName).x, TabCenter(unselectedTabName).y);
	context.Frame(host, layout);

	constexpr bool kButtonDown = true;
	constexpr bool kButtonUp = false;
	bool pressSeenPastDockSpace = false;
	imGuiIo.AddMouseButtonEvent(ImGuiMouseButton_Right, kButtonDown);
	context.Frame(host, layout, [&] { pressSeenPastDockSpace = ImGui::IsMouseClicked(ImGuiMouseButton_Right); });

	bool releaseSeenPastDockSpace = false;
	imGuiIo.AddMouseButtonEvent(ImGuiMouseButton_Right, kButtonUp);
	context.Frame(host, layout, [&] { releaseSeenPastDockSpace = ImGui::IsMouseReleased(ImGuiMouseButton_Right); });

	context.Frame(host, layout);

	CHECK(SelectedTab(host) == selectedTab);

	CHECK(pressSeenPastDockSpace);
	CHECK(releaseSeenPastDockSpace);
}

TEST_CASE("A strip width and a callback put the dock space to the right of the strip")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout;
	layout.StripWidth = 42.0f;
	int calls = 0;

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout, {}, [&] { ++calls; });
	}

	const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetDockSpaceId());

	REQUIRE(node != nullptr);
	CHECK(node->Pos.x == doctest::Approx(42.0f));
	CHECK(node->Size.x == doctest::Approx(800.0f - 42.0f));
	CHECK(calls == kSettleFrames);
}

TEST_CASE("The callback draws in a region the strip's width wide and as tall as the dock space")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout;
	layout.StripWidth = 42.0f;
	ImVec2 available;

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout, {}, [&] { available = ImGui::GetContentRegionAvail(); });
	}

	const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetDockSpaceId());

	REQUIRE(node != nullptr);
	CHECK(available.x == doctest::Approx(42.0f));
	CHECK(available.y == doctest::Approx(node->Size.y));
}

TEST_CASE("The dock space keeps one id with and without the strip so a layout saved in imgui.ini still applies")
{
	DockingContext context;
	DockSpaceHost host;
	const DockLayout withoutStrip;
	DockLayout withStrip;
	withStrip.StripWidth = 42.0f;

	context.Frame(host, withoutStrip);
	const unsigned int idWithoutStrip = host.GetDockSpaceId();

	bool stripDrawn = false;
	context.Frame(host, withStrip, {}, [&stripDrawn] { stripDrawn = true; });
	const unsigned int idWithStrip = host.GetDockSpaceId();

	REQUIRE(stripDrawn);
	REQUIRE(idWithoutStrip != 0);
	CHECK(idWithStrip == idWithoutStrip);
}

TEST_CASE("Without a callback the dock space keeps the full width")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout;
	layout.StripWidth = 42.0f;

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout);
	}

	const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetDockSpaceId());

	REQUIRE(node != nullptr);
	CHECK(node->Pos.x == doctest::Approx(0.0f));
	CHECK(node->Size.x == doctest::Approx(800.0f));
}

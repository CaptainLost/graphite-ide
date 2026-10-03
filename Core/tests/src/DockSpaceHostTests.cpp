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
	// A docking context of its own with the default font built. Frames are driven by hand:
	// a window docked by the default layout only lands in its node once it has been
	// submitted, so a check is only meaningful a few frames in.
	class DockingContext
	{
	public:
		DockingContext()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.Fonts->Build();
		}

		~DockingContext() { ImGui::DestroyContext(m_Context); }

		DockingContext(const DockingContext&) = delete;
		DockingContext& operator=(const DockingContext&) = delete;

		// One frame: the host, `insideHost` between its Begin and End, then every window the
		// layout names.
		void Frame(DockSpaceHost& host, const DockLayout& layout, const std::function<void()>& insideHost = {})
		{
			ImGui::NewFrame();

			host.Begin(layout);
			if (insideHost)
			{
				insideHost();
			}
			host.End();

			for (const std::vector<std::string>* names : {&layout.Left, &layout.Bottom, &layout.Center})
			{
				for (const std::string& name : *names)
				{
					ImGui::Begin(name.c_str());
					ImGui::End();
				}
			}

			ImGui::EndFrame();
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	constexpr int kSettleFrames = 3;

	// A panel beside the central node, so the node is a split of the dockspace rather than
	// all of it: an empty split with no window in it is merged away.
	DockLayout WithExplorer()
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
	const DockLayout layout = WithExplorer();

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
	DockLayout layout = WithExplorer();
	layout.Center = {"Document"};

	bool empty = true;
	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout, [&] { empty = host.IsCenterEmpty(); });
	}

	CHECK_FALSE(empty);
}

// Into the window DockSpace() opens inside the host, which paints the empty node's
// background: whatever the host itself drew there would sit underneath it, unseen.
TEST_CASE("CenterEmptyState claims exactly the central node, in the window that paints its background")
{
	DockingContext context;
	DockSpaceHost host;
	const DockLayout layout = WithExplorer();

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout);
	}

	ImVec2 claimed;
	ImVec2 nodeMax;
	int vertices = 0;
	int verticesBefore = 0;

	context.Frame(host, layout, [&] {
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetCenterNodeId());
		REQUIRE(node != nullptr);
		REQUIRE(node->HostWindow != nullptr);
		REQUIRE(node->HostWindow != ImGui::GetCurrentWindow());

		verticesBefore = node->HostWindow->DrawList->VtxBuffer.Size;

		CHECK_FALSE(host.CenterEmptyState("Nothing open.", "Open"));

		vertices = node->HostWindow->DrawList->VtxBuffer.Size;
		claimed = node->HostWindow->DC.CursorMaxPos;
		nodeMax = ImVec2(node->Pos.x + node->Size.x, node->Pos.y + node->Size.y);
	});

	CHECK(vertices > verticesBefore);
	CHECK(claimed.x == doctest::Approx(nodeMax.x));
	CHECK(claimed.y == doctest::Approx(nodeMax.y));
}

TEST_CASE("CenterEmptyState submits nothing while a window is docked in the central node")
{
	DockingContext context;
	DockSpaceHost host;
	DockLayout layout = WithExplorer();
	layout.Center = {"Document"};

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		context.Frame(host, layout);
	}

	context.Frame(host, layout, [&] {
		const ImGuiID before = ImGui::GetItemID();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		CHECK_FALSE(host.CenterEmptyState("Nothing open.", "Open"));

		CHECK(ImGui::GetItemID() == before);
		CHECK(ImGui::GetCursorScreenPos().x == doctest::Approx(cursor.x));
		CHECK(ImGui::GetCursorScreenPos().y == doctest::Approx(cursor.y));
	});
}

TEST_CASE("DockSpaceHost draws what it is given above the dockspace and gives the dockspace the rest")
{
	DockingContext context;
	DockSpaceHost host;
	bool drawn = false;
	float bannerBottom = 0.0f;

	const auto banner = [&] {
		drawn = true;
		ImGui::Dummy(ImVec2(10.0f, 24.0f));
		bannerBottom = ImGui::GetCursorScreenPos().y;
	};

	// A few frames: the dockspace's node takes its position from the frame it is laid out in.
	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		ImGui::NewFrame();
		host.Begin(DockLayout{}, banner);
		host.End();
		ImGui::EndFrame();
	}

	const ImGuiDockNode* node = ImGui::DockBuilderGetNode(host.GetCenterNodeId());

	CHECK(drawn);
	REQUIRE(node != nullptr);
	CHECK(node->Pos.y >= bannerBottom - 1.0f);
}

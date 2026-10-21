#include <doctest/doctest.h>

#include "Core/ImGui/DetachedWindows.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

	struct SystemRect
	{
		ImVec2 Pos;
		ImVec2 Size;
	};

	ImVec2 SystemPosOf(ImGuiViewport* viewport)
	{
		return static_cast<const SystemRect*>(viewport->PlatformUserData)->Pos;
	}

	ImVec2 SystemSizeOf(ImGuiViewport* viewport)
	{
		return static_cast<const SystemRect*>(viewport->PlatformUserData)->Size;
	}

	class ContextGuard
	{
	public:
		ContextGuard() : m_Context(ImGui::CreateContext()) {}
		~ContextGuard() { ImGui::DestroyContext(m_Context); }

		ContextGuard(const ContextGuard&) = delete;
		ContextGuard& operator=(const ContextGuard&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};

	struct DetachedScreen
	{
		DetachedScreen()
		{
			Detached.PlatformWindowCreated = true;
			Detached.Pos = ImVec2(100.0f, 100.0f);
			Detached.Size = ImVec2(400.0f, 300.0f);
			Detached.PlatformUserData = &System;
			System = SystemRect{Detached.Pos, Detached.Size};

			PlatformIo.Platform_GetWindowPos = &SystemPosOf;
			PlatformIo.Platform_GetWindowSize = &SystemSizeOf;
			PlatformIo.Viewports.push_back(&Detached);
		}

		// WARNING: ImGui asserts as a viewport is destroyed that no backend data is left on it.
		~DetachedScreen() { Detached.PlatformUserData = nullptr; }

		DetachedScreen(const DetachedScreen&) = delete;
		DetachedScreen& operator=(const DetachedScreen&) = delete;
		DetachedScreen(DetachedScreen&&) = delete;
		DetachedScreen& operator=(DetachedScreen&&) = delete;

		ContextGuard Context;
		SystemRect System;
		ImGuiViewportP Detached;
		ImGuiPlatformIO PlatformIo;
	};
}

TEST_CASE("A detached window the system moved and resized behind ImGui's back is asked to take its place from the system")
{
	DetachedScreen screen;
	screen.System = SystemRect{ImVec2(90.0f, 80.0f), ImVec2(500.0f, 375.0f)};

	CHECK(Core::AskDetachedWindowsForTheirSystemRects(screen.PlatformIo));

	CHECK(screen.Detached.PlatformRequestMove);
	CHECK(screen.Detached.PlatformRequestResize);
}

TEST_CASE("A detached window only resized by the system is asked for its size and not its position")
{
	DetachedScreen screen;
	screen.System.Size = ImVec2(500.0f, 375.0f);

	CHECK(Core::AskDetachedWindowsForTheirSystemRects(screen.PlatformIo));

	CHECK_FALSE(screen.Detached.PlatformRequestMove);
	CHECK(screen.Detached.PlatformRequestResize);
}

TEST_CASE("A detached window within a pixel of its system rect is left alone, as the backend cuts sizes to whole pixels")
{
	DetachedScreen screen;
	screen.Detached.Pos = ImVec2(100.5f, 100.5f);
	screen.Detached.Size = ImVec2(400.75f, 300.5f);

	CHECK_FALSE(Core::AskDetachedWindowsForTheirSystemRects(screen.PlatformIo));

	CHECK_FALSE(screen.Detached.PlatformRequestMove);
	CHECK_FALSE(screen.Detached.PlatformRequestResize);
}

TEST_CASE("The main window and windows not yet created or minimised are never asked")
{
	DetachedScreen screen;
	screen.System = SystemRect{ImVec2(90.0f, 80.0f), ImVec2(500.0f, 375.0f)};

	ImGuiViewportP& main = *static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
	main.PlatformUserData = &screen.System;
	main.PlatformWindowCreated = true;
	screen.PlatformIo.Viewports.push_back(&main);

	SystemRect notCreatedSystem{ImVec2(0.0f, 0.0f), ImVec2(10.0f, 10.0f)};
	ImGuiViewportP notCreated;
	notCreated.PlatformUserData = &notCreatedSystem;
	screen.PlatformIo.Viewports.push_back(&notCreated);

	screen.Detached.Flags |= ImGuiViewportFlags_IsMinimized;

	CHECK_FALSE(Core::AskDetachedWindowsForTheirSystemRects(screen.PlatformIo));

	CHECK_FALSE(main.PlatformRequestMove);
	CHECK_FALSE(main.PlatformRequestResize);
	CHECK_FALSE(notCreated.PlatformRequestResize);
	CHECK_FALSE(screen.Detached.PlatformRequestResize);

	main.PlatformUserData = nullptr;
	notCreated.PlatformUserData = nullptr;
}

TEST_CASE("A window ImGui is dragging is asked for its system size but not its position, so it keeps following the pointer")
{
	DetachedScreen screen;
	screen.System = SystemRect{ImVec2(90.0f, 80.0f), ImVec2(500.0f, 375.0f)};

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.DeltaTime = 1.0f / 60.0f;
	io.Fonts->Build();

	ImGui::NewFrame();
	ImGui::Begin("Dragged");
	ImGuiWindow* const dragged = ImGui::GetCurrentWindow();
	ImGui::End();
	ImGui::EndFrame();

	ImGuiViewportP* const ownViewport = dragged->Viewport;
	dragged->Viewport = &screen.Detached;
	ImGui::GetCurrentContext()->MovingWindow = dragged;

	CHECK(Core::AskDetachedWindowsForTheirSystemRects(screen.PlatformIo));

	CHECK_FALSE(screen.Detached.PlatformRequestMove);
	CHECK(screen.Detached.PlatformRequestResize);

	ImGui::GetCurrentContext()->MovingWindow = nullptr;
	dragged->Viewport = ownViewport;
}

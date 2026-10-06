#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

// An ImGui context of its own for the life of the test, so drawing code can be run frame by
// frame without a window or a renderer.
class ImGuiFrames
{
public:
	ImGuiFrames()
	{
		m_Context = ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(1280.0f, 720.0f);
		io.DeltaTime = 1.0f / 60.0f;
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;

		// As the application runs: with keyboard navigation on, ImGui closes the topmost popup on
		// Esc itself, before any drawing code sees the key.
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		io.Fonts->Build();
	}

	~ImGuiFrames() { ImGui::DestroyContext(m_Context); }

	ImGuiFrames(const ImGuiFrames&) = delete;
	ImGuiFrames& operator=(const ImGuiFrames&) = delete;

	void Next(const std::function<void()>& draw)
	{
		ImGui::NewFrame();
		draw();
		ImGui::EndFrame();
	}

	// One event a frame, as a person clicks: over the point, down, up.
	void Click(ImVec2 at, const std::function<void()>& draw, ImGuiMouseButton button = ImGuiMouseButton_Left)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddMousePosEvent(at.x, at.y);
		Next(draw);
		io.AddMouseButtonEvent(button, true);
		Next(draw);
		io.AddMouseButtonEvent(button, false);
		Next(draw);
	}

	void PressKey(ImGuiKey key, const std::function<void()>& draw)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(key, true);
		Next(draw);
		io.AddKeyEvent(key, false);
		Next(draw);
	}

private:
	ImGuiContext* m_Context = nullptr;
};

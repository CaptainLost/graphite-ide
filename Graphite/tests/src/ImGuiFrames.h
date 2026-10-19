#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <string_view>

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

	// ImGui keeps no item's position past its frame, so the pointer moves down the topmost popup until
	// ImGui reports the item with this label under it. False when no such item is there.
	bool ClickMenuItem(const char* label, const std::function<void()>& draw)
	{
		constexpr float kStepDown = 4.0f;
		constexpr float kIntoTheLabel = 8.0f;

		const ImGuiContext& context = *ImGui::GetCurrentContext();

		if (context.OpenPopupStack.Size == 0 || context.OpenPopupStack.back().Window == nullptr)
		{
			return false;
		}

		ImGuiWindow& menu = *context.OpenPopupStack.back().Window;
		const ImGuiID itemId = menu.GetID(label);
		const float x = menu.Pos.x + menu.WindowPadding.x + kIntoTheLabel;

		for (float y = menu.Pos.y; y < menu.Pos.y + menu.Size.y; y += kStepDown)
		{
			ImGui::GetIO().AddMousePosEvent(x, y);
			Next(draw);

			if (context.HoveredId == itemId)
			{
				Click(ImVec2(x, y), draw);
				return true;
			}
		}

		return false;
	}

	bool ClickPopupRow(int rowIndex, const std::function<void()>& draw)
	{
		constexpr float kIntoTheRow = 8.0f;
		constexpr float kHalf = 0.5f;

		const ImGuiContext& context = *ImGui::GetCurrentContext();

		if (context.OpenPopupStack.Size == 0 || context.OpenPopupStack.back().Window == nullptr)
		{
			return false;
		}

		const ImGuiWindow& popup = *context.OpenPopupStack.back().Window;
		const float rowPitch = ImGui::GetTextLineHeightWithSpacing();
		const float y =
			popup.DC.CursorStartPos.y + static_cast<float>(rowIndex) * rowPitch + ImGui::GetTextLineHeight() * kHalf;

		Click(ImVec2(popup.DC.CursorStartPos.x + kIntoTheRow, y), draw);
		return true;
	}

	static bool IsPopupNamedOpen(std::string_view name)
	{
		for (const ImGuiPopupData& popup : ImGui::GetCurrentContext()->OpenPopupStack)
		{
			if (popup.Window != nullptr && std::string_view{popup.Window->Name} == name)
			{
				return true;
			}
		}

		return false;
	}

private:
	ImGuiContext* m_Context = nullptr;
};

#include "corepch.h"

#include "Core/ImGui/TextButton.h"

#include "Core/ImGui/UiScale.h"

namespace Core {

	namespace {
		constexpr float kTextButtonRounding = 5.0f;
		constexpr float kNoBorder = 0.0f;
		constexpr int kTextButtonStyleCount = 2;

		// WARNING: The theme gives every frame a border, which fields want and a button does not, so a text button
		// takes the border off for its own frame alone.
		void PushTextButtonStyle()
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, kNoBorder);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, TextButtonRounding());
		}

		void PopTextButtonStyle()
		{
			ImGui::PopStyleVar(kTextButtonStyleCount);
		}
	}

	float TextButtonRounding()
	{
		return Scaled(kTextButtonRounding);
	}

	bool TextButton(const char* label, const ImVec2& size)
	{
		PushTextButtonStyle();
		const bool pressed = ImGui::Button(label, size);
		PopTextButtonStyle();

		return pressed;
	}

	bool SmallTextButton(const char* label)
	{
		PushTextButtonStyle();
		const bool pressed = ImGui::SmallButton(label);
		PopTextButtonStyle();

		return pressed;
	}
}

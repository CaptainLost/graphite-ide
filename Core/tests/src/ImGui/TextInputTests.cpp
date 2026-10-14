#include <doctest/doctest.h>

#include "Core/ImGui/TextInput.h"

#include <imgui.h>

#include <string>

namespace {
	class Field
	{
	public:
		Field()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.IniFilename = nullptr;
			io.Fonts->Build();
		}

		~Field()
		{
			ImGui::DestroyContext(m_Context);
		}

		Field(const Field&) = delete;
		Field& operator=(const Field&) = delete;

		bool Next(std::string& text, bool focusWithKeyboard = false)
		{
			ImGui::NewFrame();
			ImGui::Begin("Field");

			if (focusWithKeyboard)
			{
				ImGui::SetKeyboardFocusHere();
			}

			const bool changed = Core::InputTextWithHint("##field", "hint", text);

			ImGui::End();
			ImGui::Render();
			return changed;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("InputTextWithHint grows its string rather than cutting typed text off")
{
	Field field;
	std::string text = "abc";

	const bool focusWithKeyboard = true;
	field.Next(text, focusWithKeyboard);
	field.Next(text);

	const std::string typed(2000, 'x');

	for (const char typedCharacter : typed)
	{
		ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(typedCharacter));
	}

	CHECK(field.Next(text));
	CHECK(text == typed);
}

TEST_CASE("InputTextWithHint leaves an untouched string as it is")
{
	Field field;
	std::string text = "unchanged";

	CHECK_FALSE(field.Next(text));
	CHECK(text == "unchanged");
}

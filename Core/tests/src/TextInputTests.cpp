#include <doctest/doctest.h>

#include "Core/ImGui/TextInput.h"

#include <imgui.h>

#include <string>

namespace {
	// A context of its own with one window holding the field, drawn a frame at a time.
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

		bool Next(std::string& text, bool focus = false)
		{
			ImGui::NewFrame();
			ImGui::Begin("Field");

			if (focus)
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

// A fixed char buffer cut a long value off where it ended, and the cut value was then saved.
TEST_CASE("InputTextWithHint grows its string rather than cutting typed text off")
{
	Field field;
	std::string text = "abc";

	field.Next(text, true);
	field.Next(text);

	const std::string typed(2000, 'x');

	for (const char c : typed)
	{
		ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(c));
	}

	// Focusing a field from the keyboard selects what it holds, so the typing replaces "abc".
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

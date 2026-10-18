#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Explorer/RowRenameField.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace {
	struct RenameScreen
	{
		ImGuiFrames Frames;
		Graphite::RowRenameField Field;
		Graphite::RenameEnd LastEnd = Graphite::RenameEnd::StillEditing;
		float RowStartX = 0.0f;
		ImRect FieldRect;
		ImGuiID FieldId = 0;

		void Draw()
		{
			// The tree a rename is started in has the focus, as the Explorer has when its row is renamed. InputText
			// reads Enter as a shortcut, which reaches only the focused window.
			ImGui::SetNextWindowFocus();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
			ImGui::Begin("Tree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			RowStartX = ImGui::GetCursorScreenPos().x;
			const Graphite::RenameEnd end = Field.Draw("##rename");
			FieldRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			FieldId = ImGui::GetItemID();

			if (end != Graphite::RenameEnd::StillEditing)
			{
				LastEnd = end;
			}

			ImGui::Button("elsewhere");
			ImGui::End();
		}

		void DrawFrame()
		{
			Frames.Next([&] { Draw(); });
		}
	};
}

TEST_CASE("A rename field opens at the row's label with the name's stem selected")
{
	RenameScreen screen;
	REQUIRE(screen.Field.Start("notes.txt", false));
	screen.DrawFrame();
	screen.DrawFrame();

	const ImGuiInputTextState* state = ImGui::GetInputTextState(screen.FieldId);
	REQUIRE(state != nullptr);

	CHECK(state->GetSelectionStart() == 0);
	CHECK(state->GetSelectionEnd() == 5);
	CHECK(screen.FieldRect.Min.x > screen.RowStartX);
	CHECK(screen.FieldRect.Min.x <= screen.RowStartX + ImGui::GetTreeNodeToLabelSpacing());
}

TEST_CASE("A rename field is as narrow as the name, not the row")
{
	RenameScreen screen;
	REQUIRE(screen.Field.Start("a.txt", false));
	screen.DrawFrame();

	CHECK(screen.FieldRect.GetWidth() < 200.0f);
}

TEST_CASE("Enter commits the name in a rename field")
{
	RenameScreen screen;
	REQUIRE(screen.Field.Start("notes.txt", false));
	screen.DrawFrame();
	screen.DrawFrame();

	// The field's Enter is a shortcut, routed to it only a frame after the window has the focus and the field the
	// keyboard, as in the app by the time anyone presses it.
	screen.DrawFrame();
	screen.DrawFrame();
	screen.Frames.PressKey(ImGuiKey_Enter, [&] { screen.Draw(); });

	CHECK(screen.LastEnd == Graphite::RenameEnd::Committed);
	CHECK(screen.Field.Text() == "notes.txt");
}

TEST_CASE("Esc cancels a rename")
{
	RenameScreen screen;
	REQUIRE(screen.Field.Start("notes.txt", false));
	screen.DrawFrame();
	screen.DrawFrame();

	screen.Frames.PressKey(ImGuiKey_Escape, [&] { screen.Draw(); });

	CHECK(screen.LastEnd == Graphite::RenameEnd::Cancelled);
}

TEST_CASE("A click away from a rename field commits it")
{
	RenameScreen screen;
	REQUIRE(screen.Field.Start("notes.txt", false));
	screen.DrawFrame();
	screen.DrawFrame();

	screen.Frames.Click(ImVec2(300.0f, 150.0f), [&] { screen.Draw(); });

	CHECK(screen.LastEnd == Graphite::RenameEnd::Committed);
}

TEST_CASE("A rename field in a row narrower than the field's least width opens without an assertion")
{
	ImGuiFrames frames;
	Graphite::RowRenameField field;
	REQUIRE(field.Start("notes.txt", false));

	float fieldWidth = 0.0f;
	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(140.0f, 200.0f));
		ImGui::Begin("Narrow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		const float deepIndent = 90.0f;
		ImGui::Indent(deepIndent);
		field.Draw("##rename");
		fieldWidth = ImGui::GetItemRectSize().x;
		ImGui::Unindent(deepIndent);

		ImGui::End();
	});

	CHECK(fieldWidth > 0.0f);
}

TEST_CASE("A rename field in a list is one list row's pitch tall after a file row and after a folder row")
{
	for (const bool afterAFolderRow : {false, true})
	{
		CAPTURE(afterAFolderRow);

		ImGuiFrames frames;
		Graphite::RowRenameField field;
		REQUIRE(field.Start("notes.txt", false));

		float fieldTop = 0.0f;
		float nextRowTop = 0.0f;
		float pitch = 0.0f;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
			ImGui::Begin("Tree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			if (afterAFolderRow)
			{
				if (Graphite::ChevronTreeNode("folder", ImGuiTreeNodeFlags_SpanAvailWidth))
				{
					ImGui::TreePop();
				}
			}
			else
			{
				Graphite::ListRowSelectable("##file", false);
			}

			fieldTop = ImGui::GetCursorScreenPos().y;
			field.Draw("##rename");
			nextRowTop = ImGui::GetCursorScreenPos().y;
			pitch = Graphite::ListRowPitch();

			ImGui::End();
		});

		CHECK(nextRowTop - fieldTop == doctest::Approx(pitch));
	}
}

TEST_CASE("A name too long for the field does not start a rename")
{
	Graphite::RowRenameField field;

	CHECK_FALSE(field.Start(std::string(4096, 'n'), false));
}

#include <doctest/doctest.h>

#include "Editor/EditorCommandTarget.h"
#include "Editor/EditorCommands.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	class RecordingTarget final : public EditorCommandTarget
	{
	public:
		std::vector<std::string> Calls;
		bool Allowed = true;

		bool CanRun(EditorAction) override { return Allowed; }
		void Queue(EditorAction action, bool fromMenu) override
		{
			Calls.push_back(std::string{action == EditorAction::Undo ? "undo" : "other"} + (fromMenu ? " from menu" : ""));
		}
		bool CanSave() override { return Allowed; }
		void Save() override { Calls.push_back("save"); }
		bool CanClose() override { return Allowed; }
		void Close() override { Calls.push_back("close"); }
		bool CanFind() override { return Allowed; }
		void OpenFind() override { Calls.push_back("find"); }
		std::optional<std::filesystem::path> CaretFile() override { return std::filesystem::path{"a.cpp"}; }
		void GoToDefinition() override { Calls.push_back("definition"); }
		void FindReferences(ReferencesPanel&) override { Calls.push_back("references"); }
	};

	struct Harness
	{
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		ActiveEditorTarget Active;

		Harness()
		{
			RegisterEditorCommands(Commands, Keys, Active,
								   EditorCommandServices{.HasLanguageServer = [](const std::filesystem::path&) { return true; }});
		}
	};
}

TEST_CASE("An editor command acts on the active target")
{
	Harness harness;
	RecordingTarget first;
	RecordingTarget second;

	harness.Active.Set(first);
	CHECK(harness.Commands.Run(kUndoCommand));

	harness.Active.Set(second);
	CHECK(harness.Commands.Run(kUndoCommand));
	CHECK(harness.Commands.RunFromMenu(kUndoCommand));

	CHECK(first.Calls == std::vector<std::string>{"undo"});
	CHECK(second.Calls == std::vector<std::string>{"undo", "undo from menu"});
}

TEST_CASE("Save and close and find and go to definition reach the active target")
{
	Harness harness;
	RecordingTarget target;
	harness.Active.Set(target);

	CHECK(harness.Commands.Run(kSaveCommand));
	CHECK(harness.Commands.Run(kCloseCommand));
	CHECK(harness.Commands.Run("find.open"));
	CHECK(harness.Commands.Run("editor.goToDefinition"));

	CHECK(target.Calls == std::vector<std::string>{"save", "close", "find", "definition"});
}

TEST_CASE("A command the active target refuses does not run")
{
	Harness harness;
	RecordingTarget target;
	target.Allowed = false;
	harness.Active.Set(target);

	CHECK_FALSE(harness.Commands.Run(kUndoCommand));
	CHECK_FALSE(harness.Commands.Run(kSaveCommand));
	CHECK(target.Calls.empty());
}

TEST_CASE("With no active target every editor command is disabled")
{
	Harness harness;

	CHECK_FALSE(harness.Commands.Run(kUndoCommand));
	CHECK_FALSE(harness.Commands.Run("find.open"));
}

TEST_CASE("Editor commands keep their keys and scopes")
{
	Harness harness;

	CHECK(harness.Commands.KeyScopeOf(kUndoCommand) == Core::ShortcutScope::FocusedEditor);
	CHECK(harness.Commands.KeyScopeOf(kSaveCommand) == Core::ShortcutScope::FocusedEditor);
	CHECK(harness.Commands.KeyScopeOf("find.open") == Core::ShortcutScope::Global);
	CHECK(harness.Keys.FindConflicts().empty());
}

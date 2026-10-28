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
		bool CanShowLineHistory() override { return Allowed; }
		void ShowLineHistory(bool fromMenu) override
		{
			Calls.push_back(fromMenu ? "line history from menu" : "line history");
		}
	};

	struct Harness
	{
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		ActiveEditorTarget Active;

		Harness()
		{
			RegisterEditingCommands(Commands, Keys, Active);
			RegisterEditorNavigationCommands(Commands, Keys, Active,
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

TEST_CASE("Peek command Line History runs on the active editor")
{
	Harness harness;
	RecordingTarget first;
	RecordingTarget second;

	harness.Active.Set(first);
	harness.Active.Set(second);
	CHECK(harness.Commands.Run(kLineHistoryCommand));
	CHECK(harness.Commands.RunFromMenu(kLineHistoryCommand));

	second.Allowed = false;
	CHECK_FALSE(harness.Commands.Run(kLineHistoryCommand));

	CHECK(first.Calls.empty());
	CHECK(second.Calls == std::vector<std::string>{"line history", "line history from menu"});
	CHECK(harness.Keys.FindConflicts().empty());
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

TEST_CASE("The editing commands register before the navigation commands in their old order")
{
	Core::CommandRegistry commands;
	Core::Keymap keys;
	ActiveEditorTarget active;
	std::vector<std::string> ids;

	RegisterEditingCommands(commands, keys, active);
	const std::size_t editingCount = [&] {
		commands.ForEach([&](const Core::Command& command) { ids.push_back(command.Id); });
		return ids.size();
	}();
	RegisterEditorNavigationCommands(commands, keys, active, EditorCommandServices{});
	ids.clear();
	commands.ForEach([&](const Core::Command& command) { ids.push_back(command.Id); });

	std::vector<std::string> expected;
	for (const EditorCommandSpec& spec : EditorCommandSpecs())
	{
		expected.push_back(spec.Id);
	}
	expected.insert(expected.end(), {kSaveCommand, kCloseCommand});
	CHECK(editingCount == expected.size());
	expected.insert(expected.end(), {"find.open", kGoToDefinitionCommand, kFindReferencesCommand, kLineHistoryCommand});

	CHECK(ids == expected);
}

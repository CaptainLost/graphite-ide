#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using Core::Binding;
using Core::Chord;
using Core::Keymap;
using Core::ShortcutScope;
using Core::Trigger;

namespace {
	Binding ZoomInBinding()
	{
		return {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
				.Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
							   Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor)}};
	}

	bool ContainsText(const std::vector<std::string>& texts, std::string_view wantedText)
	{
		for (const std::string& text : texts)
		{
			if (text == wantedText)
			{
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("A default binding is what For returns until something overrides it")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());

	REQUIRE(keymap.For("editor.zoomIn") != nullptr);
	CHECK(keymap.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
	CHECK_FALSE(keymap.IsOverridden("editor.zoomIn"));
}

TEST_CASE("For is empty for an id nothing ever bound")
{
	Keymap keymap;

	CHECK(keymap.For("editor.zoomIn") == nullptr);
}

TEST_CASE("An override shadows the default and clearing it restores the default which the override never rewrote")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());

	keymap.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});

	CHECK(keymap.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U));
	CHECK(keymap.IsOverridden("editor.zoomIn"));
	CHECK(keymap.DefaultFor("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));

	keymap.RestoreDefault("editor.zoomIn");

	CHECK(keymap.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
	CHECK_FALSE(keymap.IsOverridden("editor.zoomIn"));
}

TEST_CASE("Clearing every override restores every default and keeps the defaults")
{
	const Binding unbound;

	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());
	keymap.SetDefault("editor.zoomOut", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus)});

	keymap.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});
	keymap.Override("editor.zoomOut", unbound);
	keymap.Override("editor.toggleWordWrap", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_K)});

	keymap.ClearOverrides();

	CHECK_FALSE(keymap.IsOverridden("editor.zoomIn"));
	CHECK_FALSE(keymap.IsOverridden("editor.zoomOut"));
	CHECK(keymap.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
	CHECK(keymap.For("editor.zoomOut")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus));
	CHECK(keymap.For("editor.toggleWordWrap") == nullptr);
}

TEST_CASE("An override can bind an id that has no default which is how a command shipped without a shortcut gets one")
{
	Keymap keymap;

	keymap.Override("editor.toggleWordWrap", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)});

	REQUIRE(keymap.For("editor.toggleWordWrap") != nullptr);
	CHECK(keymap.For("editor.toggleWordWrap")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W));
}

TEST_CASE("BoundTo finds the owner of a chord, primary or alternate")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());

	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal)) == "editor.zoomIn");
	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) == "editor.zoomIn");
	CHECK(keymap.BoundTo(Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor)) == "editor.zoomIn");
	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Q)).empty());
}

TEST_CASE("BoundTo answers about the effective binding and not the shadowed default so a chord the user moved away is free again")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());
	keymap.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});

	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)) == "editor.zoomIn");
	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal)).empty());
}

TEST_CASE("BoundTo with a scope ignores commands that cannot fire in it")
{
	Core::CommandRegistry commands;
	commands.Add({.Id = "editor.selectAll", .KeyScope = ShortcutScope::FocusedEditor});
	commands.Add({.Id = "view.findInFiles", .KeyScope = ShortcutScope::Global});

	Keymap keymap;
	keymap.SetDefault("editor.selectAll", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_A)});
	keymap.SetDefault("view.findInFiles",
					  {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F)});

	const Core::Chord selectAll = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_A);
	const Core::Chord findInFiles = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F);
	const ShortcutScope scopeTheTerminalAsksIn = ShortcutScope::Global;

	CHECK(keymap.BoundTo(selectAll, scopeTheTerminalAsksIn, commands).empty());
	CHECK(keymap.BoundTo(findInFiles, scopeTheTerminalAsksIn, commands) == "view.findInFiles");

	CHECK(keymap.BoundTo(selectAll, ShortcutScope::FocusedEditor, commands) == "editor.selectAll");
	CHECK(keymap.BoundTo(findInFiles, ShortcutScope::FocusedEditor, commands).empty());
}

TEST_CASE("BoundTo finds a chord by its gesture whatever flags the binding gave it")
{
	Keymap keymap;
	keymap.SetDefault("panel.explorer", {.Primary = Chord::Key(ImGuiKey_F).WithFlags({.FiresInTerminal = true})});

	CHECK(keymap.BoundTo(Chord::Key(ImGuiKey_F)) == "panel.explorer");
}

TEST_CASE("BoundTo asked where the keyboard is held skips a chord that waits there")
{
	Core::CommandRegistry commands;
	commands.Add({.Id = "panel.explorer"});
	commands.Add({.Id = "view.findInFiles"});

	Keymap keymap;
	keymap.SetDefault("panel.explorer", {.Primary = Chord::Key(ImGuiKey_F)});
	keymap.SetDefault("view.findInFiles", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_R).WithFlags({.FiresInTerminal = false})});

	const Core::Chord f = Chord::Key(ImGuiKey_F);
	const Core::Chord ctrlR = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_R);

	CHECK(keymap.BoundTo(f, ShortcutScope::Global, Core::TypingTarget::Nothing, commands) == "panel.explorer");
	CHECK(keymap.BoundTo(f, ShortcutScope::Global, Core::TypingTarget::Terminal, commands).empty());
	CHECK(keymap.BoundTo(ctrlR, ShortcutScope::Global, Core::TypingTarget::Editor, commands) == "view.findInFiles");
	CHECK(keymap.BoundTo(ctrlR, ShortcutScope::Global, Core::TypingTarget::Terminal, commands).empty());
}

TEST_CASE("BoundTo with a scope treats a command nobody registered as Global")
{
	Core::CommandRegistry commands;

	Keymap keymap;
	keymap.SetDefault("ghost.command", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Q)});

	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Q), ShortcutScope::Global, commands) ==
		  "ghost.command");
}

TEST_CASE("BoundTo with a scope skips an out-of-scope owner for an in-scope one")
{
	Core::CommandRegistry commands;
	commands.Add({.Id = "a.editor", .KeyScope = ShortcutScope::FocusedEditor});
	commands.Add({.Id = "b.global", .KeyScope = ShortcutScope::Global});

	Keymap keymap;
	keymap.SetDefault("a.editor", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_J)});
	keymap.SetDefault("b.global", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_J)});

	CHECK(keymap.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_J), ShortcutScope::Global, commands) == "b.global");
}

TEST_CASE("FindConflicts names both sides of a chord bound twice which is the check the application runs over its defaults at startup")
{
	Keymap keymap;
	keymap.SetDefault("document.saveAll", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});
	keymap.SetDefault("workspace.close", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());

	const std::vector<Core::BindingConflict> conflicts = keymap.FindConflicts();

	REQUIRE(conflicts.size() == 1);
	CHECK(conflicts[0].SharedChord == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S));
	CHECK(conflicts[0].FirstCommandId == "document.saveAll");
	CHECK(conflicts[0].SecondCommandId == "workspace.close");
}

TEST_CASE("FindConflicts is quiet about a table that binds nothing twice")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());
	keymap.SetDefault("document.saveAll",
					  Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});

	CHECK(keymap.FindConflicts().empty());
}

TEST_CASE("FindConflicts ignores a chord a command binds twice within itself since it reaches that one command either way")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
											   .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal)}});

	CHECK(keymap.FindConflicts().empty());
}

TEST_CASE("FindConflicts ignores commands bound to nothing which share an empty primary but cannot be reached at all")
{
	const Binding unbound;

	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", unbound);
	keymap.SetDefault("document.saveAll", unbound);

	CHECK(keymap.FindConflicts().empty());
}

TEST_CASE("ForEachOverride visits only what the user changed")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());
	keymap.SetDefault("document.saveAll", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});
	keymap.Override("document.saveAll", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)});

	std::vector<std::string> visited;
	keymap.ForEachOverride([&visited](const std::string& id, const Binding&) { visited.push_back(id); });

	REQUIRE(visited.size() == 1);
	CHECK(visited[0] == "document.saveAll");
}

TEST_CASE("ForEachBinding visits every id once, with its effective binding")
{
	Keymap keymap;
	keymap.SetDefault("editor.zoomIn", ZoomInBinding());
	keymap.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});
	keymap.Override("editor.toggleWordWrap", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)});

	std::vector<std::string> visited;
	keymap.ForEachBinding([&visited](const std::string& id, const Binding& binding) { visited.push_back(id + "=" + binding.Primary.ToString()); });

	REQUIRE(visited.size() == 2);
	CHECK(ContainsText(visited, "editor.zoomIn=Ctrl+U"));
	CHECK(ContainsText(visited, "editor.toggleWordWrap=Ctrl+W"));
}

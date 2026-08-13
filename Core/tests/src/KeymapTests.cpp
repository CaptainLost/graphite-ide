#include <doctest/doctest.h>

#include "Core/Command/Keymap.h"

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using Core::Binding;
using Core::Chord;
using Core::Keymap;
using Scope = Core::Chord::Scope;
using Core::Trigger;

namespace {
	Binding ZoomIn()
	{
		return {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
				.Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
							   Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, Scope::HoveredEditor)}};
	}
}

TEST_CASE("A default binding is what For returns until something overrides it")
{
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());

	REQUIRE(keys.For("editor.zoomIn") != nullptr);
	CHECK(keys.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
	CHECK_FALSE(keys.IsOverridden("editor.zoomIn"));
}

TEST_CASE("For is empty for an id nothing ever bound")
{
	Keymap keys;

	CHECK(keys.For("editor.zoomIn") == nullptr);
}

TEST_CASE("An override shadows the default and clearing it restores the default")
{
	// Clearing rather than rewriting the default is what makes "restore the default"
	// work and what keeps persistence to the keys the user actually changed.
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());

	keys.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});

	CHECK(keys.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U));
	CHECK(keys.IsOverridden("editor.zoomIn"));
	CHECK(keys.DefaultFor("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));

	keys.Override("editor.zoomIn", std::nullopt);

	CHECK(keys.For("editor.zoomIn")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
	CHECK_FALSE(keys.IsOverridden("editor.zoomIn"));
}

TEST_CASE("An override can bind an id that has no default")
{
	// Which is how a command shipped without a shortcut gets one.
	Keymap keys;

	keys.Override("editor.toggleWordWrap", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)});

	REQUIRE(keys.For("editor.toggleWordWrap") != nullptr);
	CHECK(keys.For("editor.toggleWordWrap")->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W));
}

TEST_CASE("BoundTo finds the owner of a chord, primary or alternate")
{
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());

	CHECK(keys.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal)) == "editor.zoomIn");
	CHECK(keys.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) == "editor.zoomIn");
	CHECK(keys.BoundTo(Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, Scope::HoveredEditor)) == "editor.zoomIn");
	CHECK(keys.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Q)).empty());
}

TEST_CASE("BoundTo answers about the effective binding, not the shadowed default")
{
	// What a rebinding screen needs: after the user moves Zoom In to Ctrl+U, Ctrl+= is
	// free, and offering to take it away from a command that no longer holds it would
	// be a lie.
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());
	keys.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});

	CHECK(keys.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)) == "editor.zoomIn");
	CHECK(keys.BoundTo(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal)).empty());
}

TEST_CASE("FindConflicts names both sides of a chord bound twice")
{
	// The check that later stops a new menu item quietly eating Ctrl+S. It cannot be a
	// test over the real table — the application's defaults are registered in Graphite,
	// which has no test project — so the application runs this at startup instead, and
	// this case is what proves the check itself works.
	Keymap keys;
	keys.SetDefault("document.saveAll", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});
	keys.SetDefault("workspace.close", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});
	keys.SetDefault("editor.zoomIn", ZoomIn());

	const std::vector<Core::Conflict> conflicts = keys.FindConflicts();

	REQUIRE(conflicts.size() == 1);
	CHECK(conflicts[0].Chord == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S));
	CHECK(conflicts[0].First == "document.saveAll");
	CHECK(conflicts[0].Second == "workspace.close");
}

TEST_CASE("FindConflicts is quiet about a table that binds nothing twice")
{
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());
	keys.SetDefault("document.saveAll",
					Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});

	CHECK(keys.FindConflicts().empty());
}

TEST_CASE("FindConflicts ignores a chord a command binds twice within itself")
{
	// Harmless, and reporting it would be noise: the same chord listed as primary and as
	// an alternate of one command reaches one command either way.
	Keymap keys;
	keys.SetDefault("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
											 .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal)}});

	CHECK(keys.FindConflicts().empty());
}

TEST_CASE("FindConflicts ignores commands that are bound to nothing")
{
	// An override that binds nothing is how the rebinding screen records a command whose
	// last chord was taken away. Two of those share an empty primary and are not in
	// conflict — neither can be reached at all — and reporting it would put an error in
	// the log at every start.
	Keymap keys;
	keys.SetDefault("editor.zoomIn", Binding{});
	keys.SetDefault("document.saveAll", Binding{});

	CHECK(keys.FindConflicts().empty());
}

TEST_CASE("ForEachOverride visits only what the user changed")
{
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());
	keys.SetDefault("document.saveAll", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});
	keys.Override("document.saveAll", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)});

	std::vector<std::string> visited;
	keys.ForEachOverride([&visited](const std::string& id, const Binding&) { visited.push_back(id); });

	REQUIRE(visited.size() == 1);
	CHECK(visited[0] == "document.saveAll");
}

TEST_CASE("ForEachBinding visits every id once, with its effective binding")
{
	Keymap keys;
	keys.SetDefault("editor.zoomIn", ZoomIn());
	keys.Override("editor.zoomIn", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_U)});
	keys.Override("editor.toggleWordWrap", Binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_W)});

	std::vector<std::string> visited;
	keys.ForEachBinding([&visited](const std::string& id, const Binding& binding) { visited.push_back(id + "=" + binding.Primary.ToString()); });

	REQUIRE(visited.size() == 2);
	CHECK(std::find(visited.begin(), visited.end(), std::string("editor.zoomIn=Ctrl+U")) != visited.end());
	CHECK(std::find(visited.begin(), visited.end(), std::string("editor.toggleWordWrap=Ctrl+W")) != visited.end());
}

#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/KeyEvent.h"

#include <string>

using Core::Chord;
using Scope = Core::Chord::Scope;

namespace {
	// A registry with one command that counts its runs, bound to Ctrl+C.
	struct Harness
	{
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		Core::ShortcutRouter Router;
		int Runs = 0;

		explicit Harness(Scope scope, bool repeats = true)
		{
			Commands.Add({.Id = "edit.copy",
						  .Label = "Copy",
						  .Run = [this] { ++Runs; },
						  .KeyScope = scope,
						  .Repeats = repeats});

			Keys.SetDefault("edit.copy", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_C)});
		}

		bool Press(Scope routedAs, Core::KeyModifier modifiers = Core::KeyModifier::Control,
				   bool repeat = false)
		{
			const Core::KeyPressedEvent event{Core::KeyCode::C, modifiers, repeat};
			return Router.OnKeyPressed(Commands, Keys, routedAs, event);
		}
	};
}

TEST_CASE("A Global command fires when routed as Global")
{
	Harness h{Scope::Global};

	CHECK(h.Press(Scope::Global));
	CHECK(h.Runs == 1);
}

// The find bar and every other text field keep their own Ctrl+C because GraphiteLayer routes
// every key event as Global, and an editor-scoped command must pass through that untouched.
TEST_CASE("A FocusedEditor command does not fire when routed as Global")
{
	Harness h{Scope::FocusedEditor};

	CHECK_FALSE(h.Press(Scope::Global));
	CHECK(h.Runs == 0);
}

TEST_CASE("A FocusedEditor command fires when the focused editor routes it")
{
	Harness h{Scope::FocusedEditor};

	CHECK(h.Press(Scope::FocusedEditor));
	CHECK(h.Runs == 1);
}

TEST_CASE("A Global command does not fire a second time when the editor routes the same stroke")
{
	Harness h{Scope::Global};

	CHECK_FALSE(h.Press(Scope::FocusedEditor));
	CHECK(h.Runs == 0);
}

TEST_CASE("A repeated press runs a repeating command again and skips a non-repeating one")
{
	Harness repeating{Scope::Global, true};
	CHECK(repeating.Press(Scope::Global, Core::KeyModifier::Control, true));
	CHECK(repeating.Runs == 1);

	Harness once{Scope::Global, false};
	CHECK_FALSE(once.Press(Scope::Global, Core::KeyModifier::Control, true));
	CHECK(once.Runs == 0);
}

// The scope belongs to the command, so a chord the user typed into settings - plain text with
// no scope in it - cannot turn an editor command global.
TEST_CASE("An overridden binding keeps the command's scope")
{
	Harness h{Scope::FocusedEditor};

	const auto chord = Chord::Parse("Ctrl+Insert");
	REQUIRE(chord);
	h.Keys.Override("edit.copy", Core::Binding{.Primary = *chord});

	const Core::KeyPressedEvent insert{Core::KeyCode::Insert, Core::KeyModifier::Control, false};

	CHECK_FALSE(h.Router.OnKeyPressed(h.Commands, h.Keys, Scope::Global, insert));
	CHECK(h.Router.OnKeyPressed(h.Commands, h.Keys, Scope::FocusedEditor, insert));
	CHECK(h.Runs == 1);
}

// Windows reports AltGr as Ctrl+Alt, so typing a Polish letter must never read as a shortcut.
TEST_CASE("Ctrl+Alt+C does not fire a Ctrl+C command")
{
	Harness h{Scope::FocusedEditor};

	CHECK_FALSE(h.Press(Scope::FocusedEditor,
						Core::KeyModifier::Control | Core::KeyModifier::Alt));
	CHECK(h.Runs == 0);
}

#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/KeyEvent.h"

#include <optional>
#include <string>
#include <vector>

using Core::Chord;
using Core::ShortcutScope;

namespace {
	enum class HeldKey
	{
		RunsAgain,
		RunsOnce
	};

	enum class StrokeKind
	{
		FirstPress,
		RepeatedPress
	};

	struct CopyBoundToCtrlC
	{
		Core::CommandRegistry Commands;
		Core::Keymap Bindings;
		Core::ShortcutRouter Router;
		int RunCount = 0;

		explicit CopyBoundToCtrlC(ShortcutScope commandScope, HeldKey heldKey = HeldKey::RunsAgain)
		{
			Commands.Add({.Id = "edit.copy",
						  .Label = "Copy",
						  .Run = [this] { ++RunCount; },
						  .KeyScope = commandScope,
						  .RepeatsWhileHeld = heldKey == HeldKey::RunsAgain});

			Bindings.SetDefault("edit.copy", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_C)});
		}

		bool Press(ShortcutScope routedScope, Core::KeyModifier modifiers = Core::KeyModifier::Control,
				   StrokeKind strokeKind = StrokeKind::FirstPress)
		{
			const bool isRepeat = strokeKind == StrokeKind::RepeatedPress;
			const Core::KeyPressedEvent pressC{Core::KeyCode::C, modifiers, isRepeat};
			return Router.OnKeyPressed(Commands, Bindings, routedScope, pressC);
		}
	};
}

TEST_CASE("A Global command fires when routed as Global")
{
	CopyBoundToCtrlC harness{ShortcutScope::Global};

	CHECK(harness.Press(ShortcutScope::Global));
	CHECK(harness.RunCount == 1);
}

TEST_CASE("A FocusedEditor command does not fire when routed as Global so other text fields keep their own Ctrl+C")
{
	CopyBoundToCtrlC harness{ShortcutScope::FocusedEditor};

	CHECK_FALSE(harness.Press(ShortcutScope::Global));
	CHECK(harness.RunCount == 0);
}

TEST_CASE("A FocusedEditor command fires when the focused editor routes it")
{
	CopyBoundToCtrlC harness{ShortcutScope::FocusedEditor};

	CHECK(harness.Press(ShortcutScope::FocusedEditor));
	CHECK(harness.RunCount == 1);
}

TEST_CASE("A Global command does not fire a second time when the editor routes the same stroke")
{
	CopyBoundToCtrlC harness{ShortcutScope::Global};

	CHECK_FALSE(harness.Press(ShortcutScope::FocusedEditor));
	CHECK(harness.RunCount == 0);
}

TEST_CASE("A binding whose command nobody registered fires only when routed as Global")
{
	Core::CommandRegistry commands;
	Core::Keymap keymap;
	Core::ShortcutRouter router;

	keymap.SetDefault("ghost.command", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Q)});

	const bool isRepeat = false;
	const Core::KeyPressedEvent pressCtrlQ{Core::KeyCode::Q, Core::KeyModifier::Control, isRepeat};

	CHECK(router.OnKeyPressed(commands, keymap, ShortcutScope::Global, pressCtrlQ));
	CHECK_FALSE(router.OnKeyPressed(commands, keymap, ShortcutScope::FocusedEditor, pressCtrlQ));
}

TEST_CASE("A repeated press runs a repeating command again and skips a non-repeating one")
{
	CopyBoundToCtrlC repeatingCopy{ShortcutScope::Global, HeldKey::RunsAgain};
	CHECK(repeatingCopy.Press(ShortcutScope::Global, Core::KeyModifier::Control, StrokeKind::RepeatedPress));
	CHECK(repeatingCopy.RunCount == 1);

	CopyBoundToCtrlC nonRepeatingCopy{ShortcutScope::Global, HeldKey::RunsOnce};
	CHECK_FALSE(nonRepeatingCopy.Press(ShortcutScope::Global, Core::KeyModifier::Control, StrokeKind::RepeatedPress));
	CHECK(nonRepeatingCopy.RunCount == 0);
}

TEST_CASE("A disabled command still consumes its chord although it does not run")
{
	Core::CommandRegistry commands;
	Core::Keymap keymap;
	Core::ShortcutRouter router;
	int copyRunCount = 0;

	commands.Add({.Id = "edit.copy",
				  .Label = "Copy",
				  .Run = [&copyRunCount] { ++copyRunCount; },
				  .Enabled = [] { return false; }});
	keymap.SetDefault("edit.copy", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_C)});

	const bool isRepeat = false;
	const Core::KeyPressedEvent pressCtrlC{Core::KeyCode::C, Core::KeyModifier::Control, isRepeat};

	CHECK(router.OnKeyPressed(commands, keymap, ShortcutScope::Global, pressCtrlC));
	CHECK(copyRunCount == 0);
}

TEST_CASE("An overridden binding keeps the command's scope since a chord typed into settings carries none")
{
	CopyBoundToCtrlC harness{ShortcutScope::FocusedEditor};

	const std::optional<Core::Chord> ctrlInsert = Chord::Parse("Ctrl+Insert");
	REQUIRE(ctrlInsert.has_value());
	harness.Bindings.Override("edit.copy", Core::Binding{.Primary = *ctrlInsert});

	const bool isRepeat = false;
	const Core::KeyPressedEvent pressCtrlInsert{Core::KeyCode::Insert, Core::KeyModifier::Control, isRepeat};

	CHECK_FALSE(harness.Router.OnKeyPressed(harness.Commands, harness.Bindings, ShortcutScope::Global, pressCtrlInsert));
	CHECK(harness.Router.OnKeyPressed(harness.Commands, harness.Bindings, ShortcutScope::FocusedEditor, pressCtrlInsert));
	CHECK(harness.RunCount == 1);
}

TEST_CASE("Ctrl+Alt+C does not fire a Ctrl+C command because Windows reports AltGr as Ctrl+Alt")
{
	CopyBoundToCtrlC harness{ShortcutScope::FocusedEditor};

	CHECK_FALSE(harness.Press(ShortcutScope::FocusedEditor,
							  Core::KeyModifier::Control | Core::KeyModifier::Alt));
	CHECK(harness.RunCount == 0);
}

TEST_CASE("A stroke the editor replays does not bring back an abandoned modifier tap")
{
	Core::CommandRegistry commands;
	Core::Keymap keymap;
	Core::ShortcutRouter router;
	int tapRunCount = 0;

	commands.Add({.Id = "app.tap", .Label = "Tap", .Run = [&tapRunCount] { ++tapRunCount; }});
	keymap.SetDefault("app.tap", {.Primary = Chord::ModifierTap(ImGuiMod_Ctrl)});

	const bool isRepeat = false;
	const Core::KeyPressedEvent pressCtrl{Core::KeyCode::LeftControl, Core::KeyModifier::Control, isRepeat};
	const Core::KeyPressedEvent pressS{Core::KeyCode::S, Core::KeyModifier::Control, isRepeat};

	router.OnKeyPressed(commands, keymap, ShortcutScope::Global, pressCtrl);
	router.OnKeyPressed(commands, keymap, ShortcutScope::Global, pressS);
	router.OnKeyPressed(commands, keymap, ShortcutScope::FocusedEditor, pressCtrl);

	const Core::KeyReleasedEvent releaseCtrl{Core::KeyCode::LeftControl, Core::KeyModifier::None};

	CHECK_FALSE(router.OnKeyReleased(commands, keymap, releaseCtrl));
	CHECK(tapRunCount == 0);
}

TEST_CASE("FindConflicts reports a Global and a FocusedEditor command sharing a chord since the menu would show it against both")
{
	CopyBoundToCtrlC harness{ShortcutScope::FocusedEditor};

	harness.Commands.Add({.Id = "app.copyPath", .Label = "Copy Path", .Run = [] {}});
	harness.Bindings.SetDefault("app.copyPath", {.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_C)});

	const std::vector<Core::BindingConflict> conflicts = harness.Bindings.FindConflicts();

	REQUIRE(conflicts.size() == 1);
	const auto& [sharedChord, firstCommandId, secondCommandId] = conflicts.front();
	CHECK(sharedChord == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_C));

	const bool copyClaimedTheChordFirst = firstCommandId == "edit.copy" && secondCommandId == "app.copyPath";
	const bool copyPathClaimedTheChordFirst = firstCommandId == "app.copyPath" && secondCommandId == "edit.copy";
	CHECK((copyClaimedTheChordFirst || copyPathClaimedTheChordFirst));
}

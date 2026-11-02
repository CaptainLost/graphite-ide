#include <doctest/doctest.h>

#include "Core/Command/Chord.h"
#include "Core/Command/ChordFlags.h"
#include "Core/Command/TypingRules.h"
#include "Core/Command/Trigger.h"

using Core::Chord;
using Core::ChordFlags;
using Core::ShortcutScope;
using Core::Trigger;
using Core::TypingTarget;

namespace {
	Chord KeyWithFlags(ImGuiKeyChord keys, const ChordFlags& flags)
	{
		return Chord::Key(keys).WithFlags(flags);
	}
}

TEST_CASE("A letter, a digit, a punctuation key or the space types a character, alone or with Shift")
{
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_F)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiMod_Shift | ImGuiKey_F)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_7)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_Slash)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_GraveAccent)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_Space)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_Keypad4)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_KeypadAdd)));
	CHECK(Core::TypesCharacter(Chord::Key(ImGuiKey_Oem102)));
}

TEST_CASE("A chord with Ctrl, Alt or the Windows key types no character, and neither do the function and editing keys")
{
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_F)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiMod_Alt | ImGuiKey_F)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiMod_Super | ImGuiKey_F)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiKey_F12)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiKey_Escape)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiKey_Enter)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Key(ImGuiKey_KeypadEnter)));
	CHECK_FALSE(Core::TypesCharacter(Chord::ModifierTap(ImGuiMod_Shift)));
	CHECK_FALSE(Core::TypesCharacter(Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor)));
}

TEST_CASE("A text field edits with its caret keys, Enter, Esc and Tab, and with its clipboard and undo keys")
{
	CHECK(Core::EditsText(Chord::Key(ImGuiKey_LeftArrow)));
	CHECK(Core::EditsText(Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_RightArrow)));
	CHECK(Core::EditsText(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Backspace)));
	CHECK(Core::EditsText(Chord::Key(ImGuiKey_Enter)));
	CHECK(Core::EditsText(Chord::Key(ImGuiKey_Escape)));
	CHECK(Core::EditsText(Chord::Key(ImGuiMod_Shift | ImGuiKey_Tab)));
	CHECK(Core::EditsText(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_V)));
	CHECK(Core::EditsText(Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)));

	CHECK_FALSE(Core::EditsText(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)));
	CHECK_FALSE(Core::EditsText(Chord::Key(ImGuiMod_Alt | ImGuiKey_LeftArrow)));
	CHECK_FALSE(Core::EditsText(Chord::Key(ImGuiKey_F12)));
}

TEST_CASE("Where nobody types, every chord fires")
{
	CHECK(Core::FiresIn(Chord::Key(ImGuiKey_F), TypingTarget::Nothing));
	CHECK(Core::FiresIn(KeyWithFlags(ImGuiKey_F, {.FiresInEditor = false}), TypingTarget::Nothing));
}

TEST_CASE("A chord that types a character waits while the editor, the terminal or a text field is typed into")
{
	const Chord f = Chord::Key(ImGuiKey_F);

	CHECK_FALSE(Core::FiresIn(f, TypingTarget::Editor));
	CHECK_FALSE(Core::FiresIn(f, TypingTarget::Terminal));
	CHECK_FALSE(Core::FiresIn(f, TypingTarget::TextField));
}

TEST_CASE("A chord that types nothing fires while the editor and the terminal are typed into")
{
	const Chord ctrlShiftE = Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E);

	CHECK(Core::FiresIn(ctrlShiftE, TypingTarget::Editor));
	CHECK(Core::FiresIn(ctrlShiftE, TypingTarget::Terminal));
	CHECK(Core::FiresIn(ctrlShiftE, TypingTarget::TextField));
}

TEST_CASE("A chord a text field edits with is left to the field but still fires in the editor")
{
	const Chord ctrlZ = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Z);

	CHECK_FALSE(Core::FiresIn(ctrlZ, TypingTarget::TextField));
	CHECK(Core::FiresIn(ctrlZ, TypingTarget::Editor));
}

TEST_CASE("A flag the user set wins over the default, each place on its own")
{
	const Chord f = KeyWithFlags(ImGuiKey_F, {.FiresInEditor = true});

	CHECK(Core::FiresIn(f, TypingTarget::Editor));
	CHECK_FALSE(Core::FiresIn(f, TypingTarget::Terminal));

	const Chord ctrlR = KeyWithFlags(ImGuiMod_Ctrl | ImGuiKey_R, {.FiresInTerminal = false});

	CHECK(Core::FiresIn(ctrlR, TypingTarget::Editor));
	CHECK_FALSE(Core::FiresIn(ctrlR, TypingTarget::Terminal));

	const Chord ctrlV = KeyWithFlags(ImGuiMod_Ctrl | ImGuiKey_V, {.FiresInTextFields = true});

	CHECK(Core::FiresIn(ctrlV, TypingTarget::TextField));
}

TEST_CASE("A chord repeats as its command does unless the user said otherwise")
{
	constexpr bool kCommandRepeats = true;
	constexpr bool kCommandRunsOnce = false;

	CHECK(Core::RepeatsWhileHeld(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_D), kCommandRepeats));
	CHECK_FALSE(Core::RepeatsWhileHeld(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_D), kCommandRunsOnce));
	CHECK_FALSE(Core::RepeatsWhileHeld(KeyWithFlags(ImGuiMod_Ctrl | ImGuiKey_D, {.RepeatsWhileHeld = false}), kCommandRepeats));
	CHECK(Core::RepeatsWhileHeld(KeyWithFlags(ImGuiMod_Ctrl | ImGuiKey_D, {.RepeatsWhileHeld = true}), kCommandRunsOnce));
}

TEST_CASE("Only a key or a modifier tap asks where it is typed, and only a key repeats")
{
	CHECK(Core::HasTypingFlags(Chord::Key(ImGuiKey_F)));
	CHECK(Core::HasTypingFlags(Chord::ModifierTap(ImGuiMod_Ctrl)));
	CHECK_FALSE(Core::HasTypingFlags(Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor)));
	CHECK_FALSE(Core::HasTypingFlags(Chord{}));

	CHECK(Core::CanRepeat(Chord::Key(ImGuiKey_F)));
	CHECK_FALSE(Core::CanRepeat(Chord::ModifierTap(ImGuiMod_Ctrl)));
}

TEST_CASE("Setting a flag back to the chord's default leaves it unset, so the chord follows the default again")
{
	ChordFlags flags;

	Core::SetFiresIn(flags, Chord::Key(ImGuiKey_F), TypingTarget::Editor, true);
	CHECK(flags.FiresInEditor == true);

	Core::SetFiresIn(flags, Chord::Key(ImGuiKey_F), TypingTarget::Editor, false);
	CHECK_FALSE(flags.FiresInEditor.has_value());
	CHECK(flags.IsEmpty());
}

TEST_CASE("Two chords of one gesture are the same gesture whatever their flags")
{
	const Chord plain = Chord::Key(ImGuiKey_F);
	const Chord flagged = KeyWithFlags(ImGuiKey_F, {.FiresInTerminal = true});

	CHECK(plain.IsSameGesture(flagged));
	CHECK_FALSE(plain == flagged);
	CHECK_FALSE(plain.IsSameGesture(Chord::Key(ImGuiKey_G)));
}

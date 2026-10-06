#include <doctest/doctest.h>

#include "Core/Command/Chord.h"

#include <optional>
#include <string>

using Core::Chord;
using Core::ShortcutScope;
using Core::Trigger;

TEST_CASE("A key chord prints its modifiers and its key")
{
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal).ToString() == "Ctrl+=");
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus).ToString() == "Ctrl+-");
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_0).ToString() == "Ctrl+0");
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S).ToString() == "Ctrl+Shift+S");
	CHECK(Chord::Key(ImGuiKey_F5).ToString() == "F5");
}

TEST_CASE("Modifiers always print in the same order whatever order their bits arrive in")
{
	const Chord chordWithModifiersOutOfOrder = Chord::Key(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P);

	CHECK(chordWithModifiersOutOfOrder.ToString() == "Ctrl+Shift+Alt+P");
}

TEST_CASE("A wheel chord prints its direction")
{
	const Chord up = Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor);
	const Chord down = Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown, ShortcutScope::HoveredEditor);

	CHECK(up.ToString() == "Ctrl+WheelUp");
	CHECK(down.ToString() == "Ctrl+WheelDown");
}

TEST_CASE("Every kind of chord survives a round trip through text")
{
	const Chord everyKindOfChord[] = {
		Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
		Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
		Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S),
		Chord::Key(ImGuiKey_F5),
		Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor),
		Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown, ShortcutScope::HoveredEditor),
	};

	for (const Chord& chord : everyKindOfChord)
	{
		const std::optional<Chord> parsed = Chord::Parse(chord.ToString());

		REQUIRE(parsed.has_value());
		CHECK(*parsed == chord);
	}
}

TEST_CASE("Parsing is forgiving about case and spacing and reads nonsense or a trailing plus as no chord rather than asserting")
{
	CHECK(Chord::Parse("ctrl + s") == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S));
	CHECK(Chord::Parse("CTRL+S") == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S));

	CHECK_FALSE(Chord::Parse("Ctrl+Nonsense").has_value());
	CHECK_FALSE(Chord::Parse("Ctrl+").has_value());
	CHECK_FALSE(Chord::Parse("").has_value());
}

TEST_CASE("Parse accepts only a keyboard key or a wheel direction as the trigger so a stored MouseLeft cannot fire at every click and a chord on a modifier key that could never fire is refused")
{
	CHECK_FALSE(Chord::Parse("MouseLeft").has_value());
	CHECK_FALSE(Chord::Parse("Ctrl+MouseLeft").has_value());
	CHECK_FALSE(Chord::Parse("MouseWheelY").has_value());
	CHECK_FALSE(Chord::Parse("GamepadStart").has_value());
	CHECK_FALSE(Chord::Parse("ModCtrl").has_value());
	CHECK_FALSE(Chord::Parse("Ctrl+ModCtrl").has_value());
	CHECK_FALSE(Chord::Parse("LeftShift").has_value());

	CHECK(Chord::Parse("Ctrl+S").has_value());
	CHECK(Chord::Parse("Ctrl+WheelUp").has_value());
}

TEST_CASE("A parsed wheel chord is scoped to the hovered editor since the text carries no scope and the one wheel gesture belongs to the editor")
{
	const std::optional<Chord> parsed = Chord::Parse("Ctrl+WheelUp");

	REQUIRE(parsed.has_value());
	CHECK(parsed->GetScope() == ShortcutScope::HoveredEditor);
}

TEST_CASE("A default-constructed chord is empty and prints as nothing")
{
	const Chord empty;

	CHECK(empty.IsEmpty());
	CHECK(empty.ToString().empty());
}

TEST_CASE("A modifier on its own round-trips as a tap chord which is not empty although it holds no key")
{
	const Chord tap = Chord::ModifierTap(ImGuiMod_Ctrl);

	CHECK_FALSE(tap.IsEmpty());
	CHECK(tap.ToString() == "Ctrl");

	const std::optional<Chord> parsed = Chord::Parse("Ctrl");

	REQUIRE(parsed.has_value());
	CHECK(*parsed == tap);
	CHECK(parsed->GetTrigger() == Trigger::ModifierTap);
}

TEST_CASE("Several modifiers tap together")
{
	const Chord tap = Chord::ModifierTap(ImGuiMod_Ctrl | ImGuiMod_Shift);

	CHECK(tap.ToString() == "Ctrl+Shift");

	const std::optional<Chord> parsed = Chord::Parse("Ctrl+Shift");

	REQUIRE(parsed.has_value());
	CHECK(*parsed == tap);
}

TEST_CASE("A tap of no modifiers is empty since it names no gesture")
{
	CHECK(Chord::ModifierTap(ImGuiMod_None).IsEmpty());
}

TEST_CASE("A key chord of modifiers alone is empty which is why a modifier tap is a trigger of its own")
{
	CHECK(Chord::Key(ImGuiMod_Ctrl).IsEmpty());
}

TEST_CASE("A wheel chord is not empty even without modifiers since the wheel direction is its gesture")
{
	CHECK_FALSE(Chord::Wheel(ImGuiMod_None, Trigger::WheelUp, ShortcutScope::HoveredEditor).IsEmpty());
}

TEST_CASE("The last segment decides between a tap and a key chord so Ctrl+S stays an ordinary chord")
{
	const std::optional<Chord> ordinary = Chord::Parse("Ctrl+S");

	REQUIRE(ordinary.has_value());
	CHECK(ordinary->GetTrigger() == Trigger::Key);
	CHECK(ordinary->ToString() == "Ctrl+S");
}

#include <doctest/doctest.h>

#include "Core/Command/Chord.h"

#include <optional>
#include <string>

using Core::Chord;
using Scope = Core::Chord::Scope;
using Core::Trigger;

TEST_CASE("A key chord prints its modifiers and its key")
{
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal).ToString() == "Ctrl+=");
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Minus).ToString() == "Ctrl+-");
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiKey_0).ToString() == "Ctrl+0");
	CHECK(Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S).ToString() == "Ctrl+Shift+S");
	CHECK(Chord::Key(ImGuiKey_F5).ToString() == "F5");
}

TEST_CASE("Modifiers always print in the same order")
{
	// Whatever order the bits arrive in, so a hand-edited settings file and a captured
	// chord cannot disagree about the spelling of the same combination.
	const Chord one = Chord::Key(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P);

	CHECK(one.ToString() == "Ctrl+Shift+Alt+P");
}

TEST_CASE("A wheel chord prints its direction")
{
	const Chord up = Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, Scope::HoveredEditor);
	const Chord down = Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown, Scope::HoveredEditor);

	CHECK(up.ToString() == "Ctrl+WheelUp");
	CHECK(down.ToString() == "Ctrl+WheelDown");
}

TEST_CASE("Every kind of chord survives a round trip through text")
{
	// The one place where a typo in the format silently eats a user's binding at the
	// next start, so every kind is checked rather than a representative one.
	const Chord chords[] = {
		Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
		Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
		Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S),
		Chord::Key(ImGuiKey_F5),
		Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, Scope::HoveredEditor),
		Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelDown, Scope::HoveredEditor),
	};

	for (const Chord& chord : chords)
	{
		const std::optional<Chord> parsed = Chord::Parse(chord.ToString());

		REQUIRE(parsed.has_value());
		CHECK(*parsed == chord);
	}
}

TEST_CASE("Parsing is forgiving about case and spacing but not about nonsense")
{
	CHECK(Chord::Parse("ctrl + s") == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S));
	CHECK(Chord::Parse("CTRL+S") == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S));

	// A hand-edited settings file must not be able to crash the IDE, so a bad value
	// reads as "no binding" rather than as an assertion.
	CHECK_FALSE(Chord::Parse("Ctrl+Nonsense").has_value());
	CHECK_FALSE(Chord::Parse("Ctrl+").has_value());
	CHECK_FALSE(Chord::Parse("").has_value());

	// "Ctrl" used to be refused here, back when a chord could only fire on a key and a
	// modifier alone therefore named nothing. It now parses as a tap — see the tap cases
	// below — so what stays refused is a trailing '+' with nothing after it.
}

TEST_CASE("Parse accepts only a keyboard key or a wheel direction as the trigger")
{
	// ImGuiKey's named range also holds mouse aliases, gamepad buttons and ImGui's
	// internal mod slots, and every one of them has a name a hand-edited file could carry.
	// A chord on MouseLeft would run its command at every click, and one on a modifier
	// could not be pressed deliberately at all — so a stored one is refused on load and
	// costs a warning, not a broken mouse.
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

TEST_CASE("A parsed wheel chord is scoped to the hovered editor")
{
	// Scope is not written into the text: there is one wheel gesture in the application
	// and it belongs to the editor. Recorded here so that changing it is a decision
	// rather than an accident.
	const std::optional<Chord> parsed = Chord::Parse("Ctrl+WheelUp");

	REQUIRE(parsed.has_value());
	CHECK(parsed->GetScope() == Scope::HoveredEditor);
}

TEST_CASE("A default-constructed chord is empty and prints as nothing")
{
	const Chord empty;

	CHECK(empty.IsEmpty());
	CHECK(empty.ToString().empty());
}

TEST_CASE("A modifier on its own round-trips as a tap chord")
{
	// The hole this closes: a chord holding only modifiers reads as empty, so binding Ctrl
	// alone produced something that displayed as unbound, never fired and did not survive a
	// reload. A distinct trigger is what makes it a chord rather than an absence — the same
	// mechanism that already lets Ctrl+WheelUp exist.
	const Core::Chord tap = Core::Chord::ModifierTap(ImGuiMod_Ctrl);

	CHECK_FALSE(tap.IsEmpty());
	CHECK(tap.ToString() == "Ctrl");

	const std::optional<Core::Chord> parsed = Core::Chord::Parse("Ctrl");

	REQUIRE(parsed.has_value());
	CHECK(*parsed == tap);
	CHECK(parsed->GetTrigger() == Core::Trigger::ModifierTap);
}

TEST_CASE("Several modifiers tap together")
{
	const Core::Chord tap = Core::Chord::ModifierTap(ImGuiMod_Ctrl | ImGuiMod_Shift);

	CHECK(tap.ToString() == "Ctrl+Shift");

	const std::optional<Core::Chord> parsed = Core::Chord::Parse("Ctrl+Shift");

	REQUIRE(parsed.has_value());
	CHECK(*parsed == tap);
}

TEST_CASE("A tap of nothing is empty")
{
	// Guards the same conflation from the other side: without this a tap chord carrying no
	// modifiers would claim to be a real binding while naming no gesture at all.
	CHECK(Core::Chord::ModifierTap(ImGuiMod_None).IsEmpty());
}

TEST_CASE("A tap chord does not collide with the key of the same modifier")
{
	// "Ctrl" is a tap; "Ctrl+S" is still an ordinary chord. The parser decides by what the
	// last segment is, so the two forms cannot be confused for one another.
	const std::optional<Core::Chord> ordinary = Core::Chord::Parse("Ctrl+S");

	REQUIRE(ordinary.has_value());
	CHECK(ordinary->GetTrigger() == Core::Trigger::Key);
	CHECK(ordinary->ToString() == "Ctrl+S");
}

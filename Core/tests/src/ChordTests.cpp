#include <doctest/doctest.h>

#include "Core/Ui/Chord.h"

#include <optional>
#include <string>

using core::Chord;
using core::Scope;
using core::Trigger;

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
    CHECK_FALSE(Chord::Parse("Ctrl").has_value());
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

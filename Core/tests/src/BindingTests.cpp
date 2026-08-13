#include <doctest/doctest.h>

#include "Core/Command/Binding.h"

#include <optional>
#include <string>

using core::Binding;
using core::Chord;
using core::ParseBinding;
using core::Scope;
using core::Trigger;

TEST_CASE("A binding of one chord is written as that chord alone")
{
    const Binding binding{.primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)};

    CHECK(core::ToString(binding) == "Ctrl+S");
}

TEST_CASE("Alternates follow the primary, separated from it")
{
    const Binding binding{.primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
                          .alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)}};

    CHECK(core::ToString(binding) == "Ctrl+=|Ctrl+Num+");

    const std::optional<Binding> parsed = ParseBinding("Ctrl+=|Ctrl+Num+");

    REQUIRE(parsed.has_value());
    CHECK(parsed->primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
    REQUIRE(parsed->alternates.size() == 1);
    CHECK(parsed->alternates[0] == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd));
}

TEST_CASE("A binding that reaches nothing round-trips through its own token")
{
    // The empty string cannot carry it: an empty text is a malformed entry, so an
    // unbound override written that way would be dropped at the next start and the
    // factory chord would silently come back.
    const Binding unbound;

    CHECK(core::ToString(unbound) == "None");

    const std::optional<Binding> parsed = ParseBinding("None");

    REQUIRE(parsed.has_value());
    CHECK(parsed->primary.IsEmpty());
    CHECK(parsed->alternates.empty());
}

TEST_CASE("The unbound token is read the way a hand-editor would write it")
{
    // The settings file is meant to be edited by hand, and Chord::Parse already reads
    // every other name in it case- and whitespace-insensitively.
    for (const char* text : {"none", " None ", "NONE"})
    {
        const std::optional<Binding> parsed = ParseBinding(text);

        REQUIRE(parsed.has_value());
        CHECK(parsed->primary.IsEmpty());
    }
}

TEST_CASE("A binding is refused whole when any part of it is unreadable")
{
    // Half a binding is worse than none: the user would keep the chords this build
    // happened to recognise and silently lose the rest.
    CHECK_FALSE(ParseBinding("Ctrl+S|Ctrl+Nonsense").has_value());
    CHECK_FALSE(ParseBinding("Ctrl+Nonsense").has_value());
    CHECK_FALSE(ParseBinding("").has_value());
    CHECK_FALSE(ParseBinding("|").has_value());
}

TEST_CASE("Every kind of chord in a binding survives a round trip through text")
{
    // The one place where a typo in the format silently eats a user's binding at the
    // next start, so the wheel gesture is carried through it too.
    const Binding binding{
        .primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
        .alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
                       Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, Scope::HoveredEditor)}};

    const std::optional<Binding> parsed = ParseBinding(core::ToString(binding));

    REQUIRE(parsed.has_value());
    CHECK(parsed->primary == binding.primary);
    REQUIRE(parsed->alternates.size() == binding.alternates.size());
    CHECK(parsed->alternates[0] == binding.alternates[0]);
    CHECK(parsed->alternates[1] == binding.alternates[1]);
}

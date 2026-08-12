#include <doctest/doctest.h>

#include "Core/Command/Chord.h"
#include "Core/Command/KeyName.h"

#include <imgui.h>

#include <string>

using core::IsModifierKey;

TEST_CASE("IsModifierKey answers for the eight modifier keys a keyboard has")
{
    CHECK(IsModifierKey(ImGuiKey_LeftCtrl));
    CHECK(IsModifierKey(ImGuiKey_LeftShift));
    CHECK(IsModifierKey(ImGuiKey_LeftAlt));
    CHECK(IsModifierKey(ImGuiKey_LeftSuper));
    CHECK(IsModifierKey(ImGuiKey_RightCtrl));
    CHECK(IsModifierKey(ImGuiKey_RightShift));
    CHECK(IsModifierKey(ImGuiKey_RightAlt));
    CHECK(IsModifierKey(ImGuiKey_RightSuper));
}

TEST_CASE("IsModifierKey answers for the reserved mod aliases as well")
{
    // The span the rebinding screen missed: these sit far from the eight above and inside
    // the named-key range, they are raised while a modifier is held, and ImGui names them.
    // Missing them turned reaching for Ctrl into a captured "Ctrl+ModCtrl".
    CHECK(IsModifierKey(ImGuiKey_ReservedForModCtrl));
    CHECK(IsModifierKey(ImGuiKey_ReservedForModShift));
    CHECK(IsModifierKey(ImGuiKey_ReservedForModAlt));
    CHECK(IsModifierKey(ImGuiKey_ReservedForModSuper));
}

TEST_CASE("IsModifierKey leaves ordinary keys alone")
{
    CHECK_FALSE(IsModifierKey(ImGuiKey_S));
    CHECK_FALSE(IsModifierKey(ImGuiKey_Equal));
    CHECK_FALSE(IsModifierKey(ImGuiKey_F5));
    CHECK_FALSE(IsModifierKey(ImGuiKey_KeypadAdd));
    CHECK_FALSE(IsModifierKey(ImGuiKey_Escape));
}

TEST_CASE("No key a capture can return names a modifier")
{
    // The failure itself, pinned. A capture walks the named-key range and takes the first
    // key it finds pressed, skipping only what IsModifierKey rejects — so no key it can
    // return may be one of ImGui's modifier names. Written as a sweep of the whole range
    // rather than a list of keys, because a list is exactly what went stale when ImGui's
    // second span of modifiers went unnoticed.
    for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index)
    {
        const auto key = static_cast<ImGuiKey>(index);

        if (IsModifierKey(key))
        {
            continue;
        }

        const std::string name = core::NameOfKey(key);

        CHECK(name.find("Mod") == std::string::npos);
        CHECK(name.find("Ctrl") == std::string::npos);
        CHECK(name.find("Shift") == std::string::npos);
        CHECK(name.find("Alt") == std::string::npos);
        CHECK(name.find("Super") == std::string::npos);
    }
}

TEST_CASE("Parse refuses a modifier as a chord's trigger")
{
    // A settings file is hand-editable, and "ModCtrl" is a name ImGui really does give a
    // key — but a chord fired by a modifier is one nothing can deliberately press.
    CHECK_FALSE(core::Chord::Parse("ModCtrl").has_value());
    CHECK_FALSE(core::Chord::Parse("Ctrl+ModCtrl").has_value());
    CHECK_FALSE(core::Chord::Parse("LeftShift").has_value());
    CHECK(core::Chord::Parse("Ctrl+S").has_value());
}

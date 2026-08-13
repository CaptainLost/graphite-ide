#include <doctest/doctest.h>

#include "Core/Command/KeyName.h"

#include <imgui.h>

#include <string>

using Core::IsModifierKey;

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

TEST_CASE("IsKeyboardKey holds at both ends of the keyboard block")
{
	// Tab opens the named range and Oem102 closes the keyboard part of it, so these two
	// are what a submodule bump would move first.
	CHECK(Core::IsKeyboardKey(ImGuiKey_Tab));
	CHECK(Core::IsKeyboardKey(ImGuiKey_Oem102));
	CHECK(Core::IsKeyboardKey(ImGuiKey_S));
	CHECK(Core::IsKeyboardKey(ImGuiKey_F12));
	CHECK(Core::IsKeyboardKey(ImGuiKey_KeypadAdd));
	CHECK(Core::IsKeyboardKey(ImGuiKey_Escape));
	CHECK(Core::IsKeyboardKey(ImGuiKey_LeftCtrl));
}

TEST_CASE("IsKeyboardKey refuses the mouse aliases")
{
	// The seven that cost a user their left click: arming a capture is itself a left
	// click, so a scan that accepted these answered its own question.
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseLeft));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseRight));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseMiddle));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseX1));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseX2));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseWheelX));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_MouseWheelY));
}

TEST_CASE("IsKeyboardKey refuses the gamepad buttons and the reserved mod slots")
{
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_GamepadStart));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_GamepadFaceDown));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_GamepadRStickDown));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_ReservedForModCtrl));
	CHECK_FALSE(Core::IsKeyboardKey(ImGuiKey_ReservedForModSuper));
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

		if (!Core::IsKeyboardKey(key) || IsModifierKey(key))
		{
			continue;
		}

		const std::string name = Core::NameOfKey(key);

		CHECK(name.find("Mod") == std::string::npos);
		CHECK(name.find("Ctrl") == std::string::npos);
		CHECK(name.find("Shift") == std::string::npos);
		CHECK(name.find("Alt") == std::string::npos);
		CHECK(name.find("Super") == std::string::npos);
	}
}

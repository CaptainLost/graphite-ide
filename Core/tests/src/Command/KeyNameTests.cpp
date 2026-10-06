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

TEST_CASE("IsModifierKey answers for the reserved mod aliases ImGui raises while a modifier is held so reaching for Ctrl never captures Ctrl+ModCtrl")
{
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

TEST_CASE("IsKeyboardKey holds at both ends of the keyboard block from Tab to Oem102 which an ImGui update would move first")
{
	CHECK(Core::IsKeyboardKey(ImGuiKey_Tab));
	CHECK(Core::IsKeyboardKey(ImGuiKey_Oem102));
	CHECK(Core::IsKeyboardKey(ImGuiKey_S));
	CHECK(Core::IsKeyboardKey(ImGuiKey_F12));
	CHECK(Core::IsKeyboardKey(ImGuiKey_KeypadAdd));
	CHECK(Core::IsKeyboardKey(ImGuiKey_Escape));
	CHECK(Core::IsKeyboardKey(ImGuiKey_LeftCtrl));
}

TEST_CASE("IsKeyboardKey refuses the mouse aliases since arming a capture is itself a left click")
{
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

TEST_CASE("No key a capture can return names a modifier anywhere in the named-key range")
{
	for (int keyValue = ImGuiKey_NamedKey_BEGIN; keyValue < ImGuiKey_NamedKey_END; ++keyValue)
	{
		const auto key = static_cast<ImGuiKey>(keyValue);

		const bool captureCanReturnKey = Core::IsKeyboardKey(key) && !IsModifierKey(key);
		if (!captureCanReturnKey)
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

#include "corepch.h"

#include "Core/Command/KeyName.h"

#include "Core/Base/Ascii.h"

namespace Core {

	namespace {
		struct KeyNameOverride
		{
			ImGuiKey Key;
			const char* Name;
		};

		constexpr KeyNameOverride kKeyNameOverrides[] = {
			{.Key = ImGuiKey_Equal, .Name = "="},
			{.Key = ImGuiKey_Minus, .Name = "-"},
			{.Key = ImGuiKey_Comma, .Name = ","},
			{.Key = ImGuiKey_Period, .Name = "."},
			{.Key = ImGuiKey_Slash, .Name = "/"},
			{.Key = ImGuiKey_Backslash, .Name = "\\"},
			{.Key = ImGuiKey_Semicolon, .Name = ";"},
			{.Key = ImGuiKey_Apostrophe, .Name = "'"},
			{.Key = ImGuiKey_LeftBracket, .Name = "["},
			{.Key = ImGuiKey_RightBracket, .Name = "]"},
			{.Key = ImGuiKey_GraveAccent, .Name = "`"},
			{.Key = ImGuiKey_KeypadAdd, .Name = "Num+"},
			{.Key = ImGuiKey_KeypadSubtract, .Name = "Num-"},
			{.Key = ImGuiKey_KeypadMultiply, .Name = "Num*"},
			{.Key = ImGuiKey_KeypadDivide, .Name = "Num/"},
			{.Key = ImGuiKey_KeypadDecimal, .Name = "Num."},
			{.Key = ImGuiKey_KeypadEnter, .Name = "NumEnter"},
		};

		constexpr std::string_view kKeypadPrefix = "Keypad";
		constexpr std::string_view kShortKeypadPrefix = "Num";

		// ImGui's named-key range also holds gamepad buttons, mouse aliases and the reserved
		// ReservedForModCtrl..ReservedForModSuper slots. The keyboard block is named by its ends
		// rather than by a list of what to skip, because such a list goes stale as ImGui adds keys.
		constexpr ImGuiKey kFirstKeyboardKey = ImGuiKey_NamedKey_BEGIN;
		constexpr ImGuiKey kLastKeyboardKey = ImGuiKey_Oem102;
	}

	std::string NameOfKey(ImGuiKey key)
	{
		for (const KeyNameOverride& entry : kKeyNameOverrides)
		{
			if (entry.Key == key)
			{
				return entry.Name;
			}
		}

		const std::string name = ImGui::GetKeyName(key);

		if (name.starts_with(kKeypadPrefix))
		{
			return std::string{kShortKeypadPrefix} + name.substr(kKeypadPrefix.size());
		}

		return name;
	}

	bool IsModifierKey(ImGuiKey key)
	{
		const bool isKeyboardModifier = key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper;
		const bool isReservedModifierSlot = key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper;

		return isKeyboardModifier || isReservedModifierSlot;
	}

	bool IsKeyboardKey(ImGuiKey key)
	{
		return key >= kFirstKeyboardKey && key <= kLastKeyboardKey;
	}

	ImGuiKey KeyNamed(std::string_view name)
	{
		for (int keyValue = ImGuiKey_NamedKey_BEGIN; keyValue < ImGuiKey_NamedKey_END; ++keyValue)
		{
			const auto key = static_cast<ImGuiKey>(keyValue);

			if (EqualsIgnoringCase(NameOfKey(key), name))
			{
				return key;
			}
		}

		return ImGuiKey_None;
	}
}

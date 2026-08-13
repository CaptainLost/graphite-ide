#include "corepch.h"

#include "Core/Command/KeyName.h"

#include "Core/Base/Ascii.h"

namespace Core {

	namespace {
		struct KeyName
		{
			ImGuiKey key;
			const char* name;
		};

		constexpr KeyName kKeyNameOverrides[] = {
			{ImGuiKey_Equal, "="},
			{ImGuiKey_Minus, "-"},
			{ImGuiKey_Comma, ","},
			{ImGuiKey_Period, "."},
			{ImGuiKey_Slash, "/"},
			{ImGuiKey_Backslash, "\\"},
			{ImGuiKey_Semicolon, ";"},
			{ImGuiKey_Apostrophe, "'"},
			{ImGuiKey_LeftBracket, "["},
			{ImGuiKey_RightBracket, "]"},
			{ImGuiKey_GraveAccent, "`"},
			{ImGuiKey_KeypadAdd, "Num+"},
			{ImGuiKey_KeypadSubtract, "Num-"},
			{ImGuiKey_KeypadMultiply, "Num*"},
			{ImGuiKey_KeypadDivide, "Num/"},
			{ImGuiKey_KeypadDecimal, "Num."},
			{ImGuiKey_KeypadEnter, "NumEnter"},
		};
	}

	std::string NameOfKey(ImGuiKey key)
	{
		for (const KeyName& entry : kKeyNameOverrides)
		{
			if (entry.key == key)
			{
				return entry.name;
			}
		}

		const std::string name = ImGui::GetKeyName(key);

		constexpr std::string_view kKeypadPrefix = "Keypad";

		if (name.rfind(kKeypadPrefix, 0) == 0)
		{
			return "Num" + name.substr(kKeypadPrefix.size());
		}

		return name;
	}

	bool IsModifierKey(ImGuiKey key)
	{
		if (key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper)
		{
			return true;
		}

		return key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper;
	}

	bool IsKeyboardKey(ImGuiKey key)
	{
		return key >= ImGuiKey_NamedKey_BEGIN && key <= ImGuiKey_Oem102;
	}

	ImGuiKey KeyNamed(std::string_view name)
	{
		const std::string wanted = Lowered(name);

		for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index)
		{
			const auto key = static_cast<ImGuiKey>(index);

			if (Lowered(NameOfKey(key)) == wanted)
			{
				return key;
			}
		}

		return ImGuiKey_None;
	}
}
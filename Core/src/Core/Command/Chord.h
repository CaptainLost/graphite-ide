#pragma once

#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	class Chord
	{
	public:
		enum class Scope : std::uint8_t
		{
			Global,
			HoveredEditor
		};

		Chord() = default;

		static Chord Key(ImGuiKeyChord keys);

		static Chord Wheel(ImGuiKeyChord mods, Trigger direction, Chord::Scope scope);

		// Modifiers on their own, fired when they are released with nothing pressed in
		// between. Empty when given no modifiers, since that names no gesture at all.
		static Chord ModifierTap(ImGuiKeyChord mods);

		Trigger GetTrigger() const { return m_Trigger; }
		Chord::Scope GetScope() const { return m_Scope; }

		ImGuiKeyChord GetKeys() const { return m_Keys; }

		bool IsEmpty() const;

		std::string ToString() const;

		static std::optional<Chord> Parse(std::string_view text);

		friend bool operator==(const Chord&, const Chord&) = default;

	private:
		ImGuiKeyChord m_Keys = 0;
		Trigger m_Trigger = Trigger::Key;
		Chord::Scope m_Scope = Chord::Scope::Global;
	};
}
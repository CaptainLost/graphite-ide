#pragma once

#include "Core/Command/ChordFlags.h"
#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	enum class ShortcutScope : std::uint8_t
	{
		Global,
		HoveredEditor,

		// Fired only by the editor holding the keyboard, which routes the strokes it reads
		// while drawing. A chord never carries it: a key chord's scope is its command's.
		FocusedEditor
	};

	class Chord
	{
	public:
		Chord() = default;

		static Chord Key(ImGuiKeyChord keys);

		static Chord Wheel(ImGuiKeyChord modifiers, Trigger direction, ShortcutScope scope);

		static Chord ModifierTap(ImGuiKeyChord modifiers);

		Trigger GetTrigger() const { return m_Trigger; }
		ShortcutScope GetScope() const { return m_Scope; }

		ImGuiKeyChord GetKeys() const { return m_Keys; }

		const ChordFlags& GetFlags() const { return m_Flags; }

		Chord WithFlags(const ChordFlags& flags) const;

		bool IsEmpty() const;

		// The keys and the trigger alone. What owns a stroke, and whether two slots hold the same shortcut, is a
		// question about the gesture; the flags only say where it may fire.
		bool IsSameGesture(const Chord& other) const;

		// The gesture alone, as menus and tooltips show it; Core::ToString(Binding) writes the flags as well.
		std::string ToString() const;

		static std::optional<Chord> Parse(std::string_view text);

		friend bool operator==(const Chord&, const Chord&) = default;

	private:
		ImGuiKeyChord m_Keys = 0;
		Trigger m_Trigger = Trigger::Key;
		ShortcutScope m_Scope = ShortcutScope::Global;
		ChordFlags m_Flags;
	};
}

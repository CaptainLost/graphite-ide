#pragma once

#include <cstdint>
#include <optional>

namespace Core {

	// What holds the keyboard when a stroke arrives. Anything but Nothing is a place the user types into.
	enum class TypingTarget : std::uint8_t
	{
		Nothing,
		Editor,
		Terminal,
		TextField
	};

	// What the user said about one chord. A flag left unset follows the chord's default, so a binding saved before a
	// flag existed reads the same as one that never touched it.
	struct ChordFlags
	{
		std::optional<bool> FiresInEditor;
		std::optional<bool> FiresInTerminal;
		std::optional<bool> FiresInTextFields;

		// Unset follows the command's own Command::RepeatsWhileHeld.
		std::optional<bool> RepeatsWhileHeld;

		bool IsEmpty() const
		{
			return !FiresInEditor.has_value() && !FiresInTerminal.has_value() && !FiresInTextFields.has_value() &&
				   !RepeatsWhileHeld.has_value();
		}

		friend bool operator==(const ChordFlags&, const ChordFlags&) = default;
	};
}

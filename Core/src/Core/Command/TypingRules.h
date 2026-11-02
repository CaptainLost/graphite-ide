#pragma once

#include "Core/Command/Chord.h"
#include "Core/Command/ChordFlags.h"

#include <optional>

namespace Core {

	// A printable key or the space, alone or with Shift: pressing it where the user types puts a character there.
	bool TypesCharacter(const Chord& chord);

	// What an ImGui text field edits with: its caret keys, Enter, Esc and Tab, and its clipboard and undo keys.
	bool EditsText(const Chord& chord);

	// A key or a modifier tap. A wheel turn is aimed by the pointer, not at whatever holds the keyboard.
	bool HasTypingFlags(const Chord& chord);

	// Only a key repeats while held; a modifier tap fires once, on the release.
	bool CanRepeat(const Chord& chord);

	// WARNING: A chord that types a character waits while the user types, or every letter bound to a command would
	// run it on its way into the text. A text field also keeps the keys it edits with.
	bool FiresInByDefault(const Chord& chord, TypingTarget target);

	std::optional<bool> FiresInFlag(const ChordFlags& flags, TypingTarget target);

	bool FiresIn(const Chord& chord, TypingTarget target);

	// A flag set back to what the chord does by default is cleared rather than stored, so the chord follows the
	// default again and the settings file names only what the user changed.
	void SetFiresIn(ChordFlags& flags, const Chord& chord, TypingTarget target, bool fires);

	bool RepeatsWhileHeld(const Chord& chord, bool commandRepeats);

	void SetRepeatsWhileHeld(ChordFlags& flags, bool repeats, bool commandRepeats);
}

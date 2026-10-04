#pragma once

#include "Core/Command/Chord.h"

#include <functional>
#include <string>

namespace Core {

	struct Command
	{
		std::string Id;
		std::string Label;

		// What a list of commands with no menu around them says instead of Label, for a command
		// whose short menu label means nothing out of its submenu. Empty means Label.
		std::string ListLabel;

		// Drawn before the label in a menu; empty leaves the label alone.
		std::string Icon;

		std::string Group;

		std::function<void()> Run;

		// What a menu item runs instead of Run, for a command that does something more when
		// chosen deliberately than when its chord is pressed - an editor's Copy confirms it from
		// a menu but not from Ctrl+C. Empty means Run.
		std::function<void()> RunFromMenu;

		std::function<bool()> Enabled;

		std::function<bool()> Checked;

		// Where this command's key chords fire. Kept on the command rather than on the chord:
		// a chord is saved to settings as plain text that carries no scope, so a scope on the
		// chord would be lost the first time the user rebinds it.
		Chord::Scope KeyScope = Chord::Scope::Global;

		// Whether holding the key runs the command again on every repeat. Zoom wants that; a
		// held Ctrl+V pasting over and over does not.
		bool Repeats = true;
	};
}
#pragma once

#include "Core/Command/Chord.h"

#include <functional>
#include <string>

namespace Core {

	struct Command
	{
		std::string Id;
		std::string Label;

		// Shown instead of Label where commands are listed outside any menu, for a command whose
		// short menu label means nothing out of its submenu. Empty means Label.
		std::string ListLabel;

		std::string Icon;

		std::string Group;

		std::function<void()> Run;

		std::function<void()> RunFromMenu;

		std::function<bool()> Enabled;

		std::function<bool()> Checked;

		// Kept on the command rather than on the chord: a chord is saved to settings as plain
		// text without a scope, so a scope on the chord would be lost the first time the user rebinds it.
		ShortcutScope KeyScope = ShortcutScope::Global;

		// Zoom wants a held key to run it again; a held Ctrl+V pasting over and over does not.
		bool RepeatsWhileHeld = true;
	};

	inline bool IsCommandEnabled(const Command& command)
	{
		if (command.Enabled == nullptr)
		{
			return true;
		}

		return command.Enabled();
	}

	inline bool IsCommandChecked(const Command& command)
	{
		if (command.Checked == nullptr)
		{
			return false;
		}

		return command.Checked();
	}
}

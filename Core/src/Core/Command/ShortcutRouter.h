#pragma once

#include "Core/Command/Chord.h"

namespace Core {

	class CommandRegistry;
	class Keymap;

	class ShortcutRouter final
	{
	public:
		void Dispatch(CommandRegistry& commands, const Keymap& keys);

		void DispatchWheel(CommandRegistry& commands, const Keymap& keys, Chord::Scope scope, float wheel);
	};
}
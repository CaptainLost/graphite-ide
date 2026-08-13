#pragma once

#include "Core/Text/Editing.h"

namespace Core {

	class CommandRegistry;
	class FrameInput;
	class Keymap;
	class ShortcutRouter;
}
namespace Core {

	struct OpenItem;

}

namespace Graphite {

	struct CommandContext
	{
		Core::CommandRegistry* Commands = nullptr;
		Core::Keymap* Keys = nullptr;
		Core::ShortcutRouter* Router = nullptr;

		// The keyboard input left over after the panels and the router have had their turn.
		// The editor reads it while drawing, which is the only moment it knows whether it is
		// the document the input was aimed at.
		const Core::FrameInput* Input = nullptr;
	};

	struct EditorFrameResult
	{
		bool Edited = false;

		bool SaveRequested = false;
	};

	EditorFrameResult DrawEditor(const CommandContext& context,
								 Core::OpenItem& item, float fontSize,
								 Core::IndentStyle indentStyle,
								 bool wordWrap);
}
#pragma once

#include "Core/Text/Editing.h"

namespace Core {

	class CommandRegistry;
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
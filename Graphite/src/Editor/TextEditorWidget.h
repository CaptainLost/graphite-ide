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
		Core::CommandRegistry* commands = nullptr;
		Core::Keymap* keys = nullptr;
		Core::ShortcutRouter* router = nullptr;
	};

	struct EditorFrameResult
	{
		bool edited = false;

		bool saveRequested = false;
	};

	EditorFrameResult DrawEditor(const CommandContext& context,
								 Core::OpenItem& item, float fontSize,
								 Core::IndentStyle indentStyle,
								 bool wordWrap);
}
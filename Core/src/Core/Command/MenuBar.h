#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Command/MenuItem.h"

#include <functional>
#include <vector>

namespace Core {

	class CommandRegistry;
	class Keymap;

	// Draws into whichever menu or popup is open. The menu bar's dropdowns and every context menu
	// built from commands draw through this, so the two can never disagree about how a command looks.
	void RenderMenuItems(CommandRegistry& commands, const Keymap& keymap,
						 const std::vector<MenuItem>& items);

	class MenuBar final : NonCopyable
	{
	public:
		void Render(CommandRegistry& commands, const Keymap& keymap,
					const std::vector<MenuItem>& menuTree, float rowHeight,
					const std::function<void()>& drawLeading, const std::function<void()>& drawTrailing);
	};
}

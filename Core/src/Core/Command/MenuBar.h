#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Command/MenuItem.h"

#include <functional>
#include <vector>

namespace Core {

	class CommandRegistry;
	class Keymap;

	// Draws a run of menu items - commands with their bound shortcuts and enabled state,
	// separators, sections, groups and submenus - into whatever menu or popup is open. The
	// menu bar's dropdowns and every context menu built from commands draw through this, so
	// the two can never disagree about how a command looks.
	void RenderMenuItems(CommandRegistry& commands, const Keymap& keys,
						 const std::vector<MenuItem>& items);

	class MenuBar final : NonCopyable
	{
	public:
		void Render(CommandRegistry& commands, const Keymap& keys,
					const std::vector<MenuItem>& tree, float rowHeight,
					const std::function<void()>& trailing);
	};
}
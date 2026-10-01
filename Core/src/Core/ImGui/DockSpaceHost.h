#pragma once

#include "Core/Base/NonCopyable.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct DockLayout
	{
		std::vector<std::string> Left;
		std::vector<std::string> Bottom;
		std::vector<std::string> Center;

		float LeftRatio = 0.20f;
		float BottomRatio = 0.25f;

		float MenuBarHeight = 0.0f;
	};

	class DockSpaceHost final : NonCopyable
	{
	public:
		// `aboveDockSpace`, when given, is drawn at the top of the host window, below the menu
		// bar, and the dockspace takes the space left under it - a place for a bar that has to
		// sit above every docked panel without being one.
		void Begin(const DockLayout& layout, const std::function<void()>& aboveDockSpace = {});
		void End();

		void RequestLayoutReset()
		{
			m_LayoutBuilt = false;
			m_ForceRebuild = true;
		}

		unsigned int GetCenterNodeId() const;

		// The dockspace's root node, 0 before the first Begin.
		unsigned int GetDockSpaceId() const { return m_DockSpaceId; }

		// Between Begin and End. True while the central node holds no window, so the area a
		// document would open into shows nothing but the dockspace's background.
		bool IsCenterEmpty() const;

		// Between Begin and End. While the central node is empty, says why in its middle
		// through EmptyState, with `action` as a button under the message; returns true on
		// the frame that button is clicked. Submits nothing while the node holds a window.
		bool CenterEmptyState(std::string_view message, const char* action = nullptr) const;

	private:
		bool m_LayoutBuilt = false;
		bool m_ForceRebuild = false;

		unsigned int m_DockSpaceId = 0;
	};
}
#pragma once

#include "Core/Base/NonCopyable.h"

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
		void Begin(const DockLayout& layout);
		void End();

		void RequestLayoutReset()
		{
			m_LayoutBuilt = false;
			m_ForceRebuild = true;
		}

		unsigned int GetCenterNodeId() const;

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
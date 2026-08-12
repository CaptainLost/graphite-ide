#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
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

	private:
		bool m_LayoutBuilt = false;
		bool m_ForceRebuild = false;

		unsigned int m_DockSpaceId = 0;
	};
}
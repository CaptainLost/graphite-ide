#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <vector>

namespace Core {

	struct DockLayout
	{
		std::vector<std::string> left;
		std::vector<std::string> bottom;
		std::vector<std::string> center;

		float leftRatio = 0.20f;
		float bottomRatio = 0.25f;

		float menuBarHeight = 0.0f;
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
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

		float StripWidth = 0.0f;
	};

	class DockSpaceHost final : NonCopyable
	{
	public:
		void Begin(const DockLayout& layout, const std::function<void()>& aboveDockSpace = {},
				   const std::function<void()>& besideDockSpace = {});
		void End();

		void RequestLayoutReset()
		{
			m_LayoutBuilt = false;
			m_ForceRebuild = true;
		}

		unsigned int GetCenterNodeId() const;

		bool HasBuiltLayoutThisFrame() const { return m_BuiltLayoutThisFrame; }

		unsigned int GetDockSpaceId() const { return m_DockSpaceId; }

		bool IsCenterEmpty() const;

		bool CenterEmptyState(std::string_view message, const char* action = nullptr,
							  const char* noteWhileDisabled = nullptr) const;

	private:
		bool m_LayoutBuilt = false;
		bool m_ForceRebuild = false;
		bool m_BuiltLayoutThisFrame = false;

		unsigned int m_DockSpaceId = 0;
	};
}

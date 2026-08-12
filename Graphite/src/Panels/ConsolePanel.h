#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/UI/Panel.h"

#include <imgui.h>

#include <memory>
#include <vector>

namespace Graphite {

	class ConsolePanel final : public Core::Panel
	{
	public:
		explicit ConsolePanel(Core::Ref<Core::RingBufferSink> sink);

		void OnImGuiRender() override;

	private:
		void RenderToolbar();
		void RenderEntries();

		Core::Ref<Core::RingBufferSink> m_Sink;
		std::vector<Core::LogEntry> m_Entries;
		ImGuiTextFilter m_Filter;
		bool m_AutoScroll = true;
		std::size_t m_LastCount = 0;
	};
}
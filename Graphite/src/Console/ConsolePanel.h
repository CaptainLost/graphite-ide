#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/ImGui/SelectionGesture.h"
#include "Core/Text/TextSelection.h"
#include "Core/UI/Panel.h"

#include "Console/ConsoleLevel.h"
#include "Console/ConsoleLines.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	class EventBus;
}

namespace Graphite {

	class ConsolePanel final : public Core::Panel
	{
	public:
		ConsolePanel(Core::Ref<Core::RingBufferSink> sink, Core::EventBus& events);

		void OnImGuiRender() override;

	private:
		void RenderToolbar();
		void RenderLevelCombo();
		void RenderEntries();
		void CollectShownEntries();
		void UpdateSelection(const ConsoleLayout& layout);
		void FollowTailOnlyWhenEntriesArrived();

		Core::Ref<Core::RingBufferSink> m_Sink;
		std::vector<Core::LogEntry> m_Entries;
		std::vector<Core::LogEntry> m_Shown;
		Core::EventBus& m_Events;
		Core::TextSelection m_Selection;
		Core::SelectionGesture m_Gesture;
		ImGuiTextFilter m_Filter;
		std::string m_FilterText;
		Core::LogLevel m_MinimumLevel = kDefaultConsoleLevel;
		bool m_AutoScroll = true;
		std::size_t m_LastCount = 0;

		std::optional<std::uint64_t> m_SeenRevision;
		std::string m_SeenFilter;
		Core::LogLevel m_SeenLevel = kDefaultConsoleLevel;
	};
}

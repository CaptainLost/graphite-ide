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
		// What the filter leaves of m_Entries, which is what the panel draws and what a
		// selection is taken from: a message the reader cannot see is not one they can copy.
		std::vector<Core::LogEntry> m_Shown;
		Core::EventBus& m_Events;
		// Lines are entry sequence numbers, columns are tab-expanded characters (ADR 0503).
		Core::TextSelection m_Selection;
		Core::SelectionGesture m_Gesture;
		ImGuiTextFilter m_Filter;
		// What the filter box holds. ImGuiTextFilter has no hint of its own, so the box is drawn
		// here and the text is handed to the filter to be parsed.
		std::string m_FilterText;
		Core::LogLevel m_MinimumLevel = kDefaultConsoleLevel;
		bool m_AutoScroll = true;
		std::size_t m_LastCount = 0;

		// The sink's revision and the filter text m_Entries and m_Shown were last built from.
		// The snapshot is the whole buffer, so it is taken again only when one of them moved.
		std::optional<std::uint64_t> m_SeenRevision;
		std::string m_SeenFilter;
		Core::LogLevel m_SeenLevel = kDefaultConsoleLevel;
	};
}

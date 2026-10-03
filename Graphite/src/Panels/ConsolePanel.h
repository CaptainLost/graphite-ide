#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/UI/Panel.h"

#include "Panels/ConsoleSelection.h"

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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
		void CollectShownEntries();
		void HandleSweep(const ConsoleLayout& layout);
		void HandleShortcuts();
		void RenderContextMenu();
		void CopySelection() const;
		void FollowTailOnlyWhenEntriesArrived();

		Core::Ref<Core::RingBufferSink> m_Sink;
		std::vector<Core::LogEntry> m_Entries;
		// What the filter leaves of m_Entries, which is what the panel draws and what a
		// selection is taken from: a message the reader cannot see is not one they can copy.
		std::vector<Core::LogEntry> m_Shown;
		ConsoleSelection m_Selection;
		// Set while the left button is held after a press inside the output, so a drag that
		// leaves the panel keeps extending the selection instead of dropping it.
		bool m_Sweeping = false;
		ImGuiTextFilter m_Filter;
		bool m_AutoScroll = true;
		std::size_t m_LastCount = 0;

		// The sink's revision and the filter text m_Entries and m_Shown were last built from.
		// The snapshot is the whole buffer, so it is taken again only when one of them moved.
		std::optional<std::uint64_t> m_SeenRevision;
		std::string m_SeenFilter;
	};
}

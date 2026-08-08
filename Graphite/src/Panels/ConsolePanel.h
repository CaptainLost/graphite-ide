#pragma once

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Ui/Panel.h"

#include <imgui.h>

#include <memory>
#include <vector>

namespace graphite
{
    /// View of the engine logs. Reads the ring buffer filled by Logger.
    class ConsolePanel final : public core::Panel
    {
    public:
        explicit ConsolePanel(std::shared_ptr<core::RingBufferSink> sink);

        void OnUIRender() override;

    private:
        void RenderToolbar();
        void RenderEntries();

        std::shared_ptr<core::RingBufferSink> m_sink;
        std::vector<core::LogEntry> m_entries;
        ImGuiTextFilter m_filter;
        bool m_autoScroll = true;
        std::size_t m_lastCount = 0;
    };
}

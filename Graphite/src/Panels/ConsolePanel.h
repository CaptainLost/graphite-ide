#pragma once

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Ui/Panel.h"

#include <imgui.h>

#include <memory>
#include <vector>

namespace Graphite
{
    /// View of the engine logs. Reads the ring buffer filled by Logger.
    class ConsolePanel final : public Core::Panel
    {
    public:
        explicit ConsolePanel(std::shared_ptr<Core::RingBufferSink> sink);

        void OnUIRender() override;

    private:
        void RenderToolbar();
        void RenderEntries();

        std::shared_ptr<Core::RingBufferSink> m_sink;
        std::vector<Core::LogEntry> m_entries;
        ImGuiTextFilter m_filter;
        bool m_autoScroll = true;
        std::size_t m_lastCount = 0;
    };
}

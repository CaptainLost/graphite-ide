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

        std::shared_ptr<Core::RingBufferSink> m_Sink;
        std::vector<Core::LogEntry> m_Entries;
        ImGuiTextFilter m_Filter;
        bool m_AutoScroll = true;
        std::size_t m_LastCount = 0;
    };
}

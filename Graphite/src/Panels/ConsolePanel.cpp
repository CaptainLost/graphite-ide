#include "Panels/ConsolePanel.h"

#include <utility>

namespace graphite
{
    namespace
    {
        ImVec4 ColorFor(core::LogLevel level)
        {
            switch (level)
            {
            case core::LogLevel::Trace: return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
            case core::LogLevel::Info:  return ImVec4(0.84f, 0.86f, 0.89f, 1.0f);
            case core::LogLevel::Warn:  return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
            case core::LogLevel::Error: return ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
            }

            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    ConsolePanel::ConsolePanel(std::shared_ptr<core::RingBufferSink> sink)
        : core::Panel("console", "Console"), m_sink(std::move(sink))
    {
    }

    void ConsolePanel::OnUIRender()
    {
        m_entries = m_sink->Snapshot();

        RenderToolbar();
        ImGui::Separator();
        RenderEntries();
    }

    void ConsolePanel::RenderToolbar()
    {
        if (ImGui::Button("Clear"))
        {
            m_sink->Clear();
            m_entries.clear();
            m_lastCount = 0;
        }

        ImGui::SameLine();

        if (ImGui::Button("Copy all"))
        {
            std::string joined;
            for (const core::LogEntry& entry : m_entries)
            {
                joined.append(core::ToString(entry.level));
                joined.append(" ");
                joined.append(entry.message);
                joined.append("\n");
            }

            ImGui::SetClipboardText(joined.c_str());
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        m_filter.Draw("##consoleFilter");

        ImGui::SameLine();
        ImGui::TextDisabled("%d entries", static_cast<int>(m_entries.size()));
    }

    void ConsolePanel::RenderEntries()
    {
        if (!ImGui::BeginChild("##consoleOutput", ImVec2(0.0f, 0.0f),
                               ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::EndChild();
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

        for (const core::LogEntry& entry : m_entries)
        {
            if (!m_filter.PassFilter(entry.message.c_str()))
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(entry.level));
            ImGui::TextUnformatted(entry.message.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar();

        // We only scroll after a new entry is written, so we don't block
        // manual scrolling upward.
        if (m_autoScroll && m_entries.size() != m_lastCount)
            ImGui::SetScrollHereY(1.0f);

        m_lastCount = m_entries.size();

        ImGui::EndChild();
    }
}

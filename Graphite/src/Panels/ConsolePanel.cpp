#include "Core/Base/Base.h"

#include "Panels/ConsolePanel.h"

#include <utility>

namespace Graphite {

	namespace {
		ImVec4 ColorFor(Core::LogLevel level)
		{
			switch (level)
			{
			case Core::LogLevel::Trace:
				return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
			case Core::LogLevel::Info:
				return ImVec4(0.84f, 0.86f, 0.89f, 1.0f);
			case Core::LogLevel::Warn:
				return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
			case Core::LogLevel::Error:
				return ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
			}

			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	ConsolePanel::ConsolePanel(Core::Ref<Core::RingBufferSink> sink)
		: Core::Panel("console", "Console"), m_Sink(std::move(sink))
	{
	}

	void ConsolePanel::OnImGuiRender()
	{
		m_Entries = m_Sink->TakeSnapshot();

		RenderToolbar();
		ImGui::Separator();
		RenderEntries();
	}

	void ConsolePanel::RenderToolbar()
	{
		if (ImGui::Button("Clear"))
		{
			m_Sink->Clear();
			m_Entries.clear();
			m_LastCount = 0;
		}

		ImGui::SameLine();

		if (ImGui::Button("Copy all"))
		{
			std::string joined;
			for (const Core::LogEntry& entry : m_Entries)
			{
				joined.append(Core::ToString(entry.Level));
				joined.append(" ");
				joined.append(entry.Message);
				joined.append("\n");
			}

			ImGui::SetClipboardText(joined.c_str());
		}

		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(240.0f);
		m_Filter.Draw("##consoleFilter");

		ImGui::SameLine();
		ImGui::TextDisabled("%d entries", static_cast<int>(m_Entries.size()));
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

		for (const Core::LogEntry& entry : m_Entries)
		{
			if (!m_Filter.PassFilter(entry.Message.c_str()))
				continue;

			ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(entry.Level));
			ImGui::TextUnformatted(entry.Message.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::PopStyleVar();

		// We only scroll after a new entry is written, so we don't block
		// manual scrolling upward.
		if (m_AutoScroll && m_Entries.size() != m_LastCount)
			ImGui::SetScrollHereY(1.0f);

		m_LastCount = m_Entries.size();

		ImGui::EndChild();
	}
}
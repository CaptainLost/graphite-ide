#include "Settings/ClaudeSection.h"

#include "Sessions/ClaudeSettings.h"

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"

#include <imgui.h>

#include <cstddef>
#include <iterator>
#include <map>

namespace Graphite {

	ClaudeSection::ClaudeSection(ClaudeSettings& settings) : m_Settings(settings) {}

	void ClaudeSection::OnImGuiRender()
	{
		RenderBinary();
		RenderPrices();
	}

	void ClaudeSection::RenderBinary()
	{
		ImGui::SeparatorText("Program");

		if (!m_EditingBinary)
		{
			m_PendingBinary = Core::ToUtf8(m_Settings.GetBinary());
		}

		char buffer[1024] = {};
		m_PendingBinary.copy(buffer, sizeof(buffer) - 1);

		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::InputTextWithHint("##claudeBinary", "claude.exe from PATH", buffer, sizeof(buffer)))
			m_PendingBinary = buffer;

		m_EditingBinary = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			m_Settings.SetBinary(Core::FromUtf8(m_PendingBinary));
		}

		if (!m_ResolvedFor.has_value() || *m_ResolvedFor != m_Settings.GetBinary())
		{
			m_ResolvedFor = m_Settings.GetBinary();
			m_Resolved = m_Settings.ResolveProgram();
		}

		if (m_Resolved.empty())
			ImGui::TextDisabled("Claude was not found. Name claude.exe above, or put it on PATH.");
		else
			ImGui::TextDisabled("Runs %s", Core::ToUtf8(m_Resolved).c_str());
	}

	void ClaudeSection::RenderPrices()
	{
		ImGui::SeparatorText("Prices, dollars per million tokens");

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;

		if (!ImGui::BeginTable("##claudePrices", 7, kFlags))
			return;

		ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		for (const char* column : {"Input", "Output", "Write 5m", "Write 1h", "Read"})
			ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		// Copied first: SetPrice and ResetPrice change the map being walked.
		const std::map<std::string, Price> entries = m_Settings.GetPricing().GetEntries();

		for (const auto& [model, price] : entries)
		{
			ImGui::PushID(model.c_str());
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(model.c_str());

			Price edited = price;
			double* fields[] = {&edited.Input, &edited.Output, &edited.CacheWrite5m, &edited.CacheWrite1h,
								&edited.CacheRead};

			bool committed = false;

			for (std::size_t index = 0; index < std::size(fields); ++index)
			{
				ImGui::TableNextColumn();
				ImGui::PushID(static_cast<int>(index));
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputDouble("##price", fields[index], 0.0, 0.0, "%.3f");
				committed = committed || ImGui::IsItemDeactivatedAfterEdit();
				ImGui::PopID();
			}

			if (committed && !(edited == price))
				m_Settings.SetPrice(model, edited);

			ImGui::TableNextColumn();

			if (ImGui::SmallButton("Reset"))
				m_Settings.ResetPrice(model);

			ImGui::PopID();
		}

		RenderNewPrice();
		ImGui::EndTable();
	}

	void ClaudeSection::RenderNewPrice()
	{
		ImGui::PushID("##newPrice");
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		char buffer[128] = {};
		m_NewModel.copy(buffer, sizeof(buffer) - 1);
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::InputTextWithHint("##model", "model id, e.g. claude-opus-5", buffer, sizeof(buffer)))
			m_NewModel = buffer;

		double* fields[] = {&m_NewPrice.Input, &m_NewPrice.Output, &m_NewPrice.CacheWrite5m,
							&m_NewPrice.CacheWrite1h, &m_NewPrice.CacheRead};

		for (std::size_t index = 0; index < std::size(fields); ++index)
		{
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(index));
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputDouble("##price", fields[index], 0.0, 0.0, "%.3f");
			ImGui::PopID();
		}

		ImGui::TableNextColumn();

		// An empty id would price nothing, and a model's id is matched exactly, so surrounding
		// spaces from a paste are not part of it.
		const std::string model = Core::Trimmed(m_NewModel);
		ImGui::BeginDisabled(model.empty());

		if (ImGui::SmallButton("Add"))
		{
			m_Settings.SetPrice(model, m_NewPrice);
			m_NewModel.clear();
			m_NewPrice = {};
		}

		ImGui::EndDisabled();
		ImGui::PopID();
	}
}

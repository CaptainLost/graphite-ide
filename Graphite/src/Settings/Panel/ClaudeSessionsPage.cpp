#include "Settings/Panel/ClaudeSessionsPage.h"

#include "Sessions/ClaudeSettings.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextInput.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <cstddef>
#include <iterator>
#include <map>

namespace Graphite {

	const char* ClaudeSessionsPage::GetIcon() const
	{
		return ICON_MDI_ROBOT_OUTLINE;
	}

	std::vector<SettingsCard> ClaudeSessionsPage::Describe()
	{
		RefreshResolved();

		SettingsRow binary{.Title = "Claude program", .Keywords = {"claude", "path", "executable"}, .WideControl = true, .DrawControl = [this] { DrawBinary(); }};

		if (m_Resolved.empty())
		{
			binary.Problem = "Claude was not found. Name claude.exe here, or put it on PATH.";
		}
		else
		{
			binary.Description = "Runs " + Core::ToUtf8(m_Resolved) + ".";
		}

		SettingsCard program{.Heading = "Program"};
		program.Rows.push_back(std::move(binary));

		SettingsCard prices{.Heading = "Prices", .Note = "Dollars per million tokens."};
		prices.Rows.push_back(SettingsRow{.Title = "Prices", .Keywords = {"price", "cost", "model", "token"}, .DrawBlock = [this] { DrawPrices(); }});

		return {program, prices};
	}

	void ClaudeSessionsPage::RefreshResolved()
	{
		if (m_ResolvedFor.has_value() && *m_ResolvedFor == m_Settings.GetBinary())
		{
			return;
		}

		m_ResolvedFor = m_Settings.GetBinary();
		m_Resolved = m_Settings.ResolveProgram();
	}

	void ClaudeSessionsPage::DrawBinary()
	{
		if (!m_EditingBinary)
		{
			m_PendingBinary = Core::ToUtf8(m_Settings.GetBinary());
		}

		Core::InputTextWithHint("##claudeBinary", "claude.exe from PATH", m_PendingBinary);
		m_EditingBinary = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			m_Settings.SetBinary(Core::FromUtf8(m_PendingBinary));
		}
	}

	void ClaudeSessionsPage::DrawPrices()
	{
		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;

		if (!ImGui::BeginTable("##claudePrices", 7, kFlags))
		{
			return;
		}

		ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		for (const char* column : {"Input", "Output", "Write 5m", "Write 1h", "Read"})
		{
			ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		}
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
			double* fields[] = {&edited.Input, &edited.Output, &edited.CacheWrite5m, &edited.CacheWrite1h, &edited.CacheRead};

			bool committed = false;

			for (std::size_t fieldIndex = 0; fieldIndex < std::size(fields); ++fieldIndex)
			{
				ImGui::TableNextColumn();
				ImGui::PushID(static_cast<int>(fieldIndex));
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputDouble("##price", fields[fieldIndex], 0.0, 0.0, "%.3f");
				committed = committed || ImGui::IsItemDeactivatedAfterEdit();
				ImGui::PopID();
			}

			if (committed && !(edited == price))
			{
				m_Settings.SetPrice(model, edited);
			}

			ImGui::TableNextColumn();

			if (ImGui::SmallButton("Reset"))
			{
				m_Settings.ResetPrice(model);
			}

			ImGui::PopID();
		}

		DrawNewPrice();
		ImGui::EndTable();
	}

	void ClaudeSessionsPage::DrawNewPrice()
	{
		ImGui::PushID("##newPrice");
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-1.0f);
		Core::InputTextWithHint("##model", "model id, e.g. claude-opus-5", m_NewModel);

		double* fields[] = {&m_NewPrice.Input, &m_NewPrice.Output, &m_NewPrice.CacheWrite5m, &m_NewPrice.CacheWrite1h, &m_NewPrice.CacheRead};

		for (std::size_t fieldIndex = 0; fieldIndex < std::size(fields); ++fieldIndex)
		{
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(fieldIndex));
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputDouble("##price", fields[fieldIndex], 0.0, 0.0, "%.3f");
			ImGui::PopID();
		}

		ImGui::TableNextColumn();

		// An empty id would price nothing, and a model's id is matched exactly, so surrounding spaces
		// from a paste are not part of it.
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

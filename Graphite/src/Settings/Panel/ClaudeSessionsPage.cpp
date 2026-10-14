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

	namespace {

		constexpr double kNoStep = 0.0;
		constexpr double kNoFastStep = 0.0;
		constexpr const char* kPriceFormat = "%.3f";
	}

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
			m_BinaryBeingTyped = Core::ToUtf8(m_Settings.GetBinary());
		}

		Core::InputTextWithHint("##claudeBinary", "claude.exe from PATH", m_BinaryBeingTyped);
		m_EditingBinary = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			m_Settings.SetBinary(Core::FromUtf8(m_BinaryBeingTyped));
		}
	}

	void ClaudeSessionsPage::DrawPrices()
	{
		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
		constexpr int kColumnCount = 7;
		constexpr float kModelColumnWeight = 3.0f;
		constexpr float kPriceColumnWeight = 1.0f;

		if (!ImGui::BeginTable("##claudePrices", kColumnCount, kTableFlags))
		{
			return;
		}

		ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch, kModelColumnWeight);
		for (const char* column : {"Input", "Output", "Write 5m", "Write 1h", "Read"})
		{
			ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthStretch, kPriceColumnWeight);
		}
		ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		const std::map<std::string, Price> entriesCopiedBecauseEditsChangeTheMap = m_Settings.GetPricing().GetEntries();

		for (const auto& [model, price] : entriesCopiedBecauseEditsChangeTheMap)
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
				ImGui::InputDouble("##price", fields[fieldIndex], kNoStep, kNoFastStep, kPriceFormat);
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
			ImGui::InputDouble("##price", fields[fieldIndex], kNoStep, kNoFastStep, kPriceFormat);
			ImGui::PopID();
		}

		ImGui::TableNextColumn();

		const std::string model = Core::Trimmed(m_NewModel);
		const bool modelIdIsEmpty = model.empty();
		ImGui::BeginDisabled(modelIdIsEmpty);

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

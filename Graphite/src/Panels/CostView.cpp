#include "Core/Base/Base.h"

#include "Panels/CostView.h"

#include "Sessions/SessionText.h"

#include <imgui.h>
#include <implot.h>

#include <array>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		CostResult Sum(const Pricing& pricing, const UsageByModel& usage)
		{
			return pricing.CostOf(usage);
		}

		void DrawTotals(const Session& session, const std::optional<SessionSummary>& summary, const Pricing& pricing)
		{
			// The session's own usage comes from the summary when there is one — it covers the
			// whole file — and from the turns read so far otherwise.
			UsageByModel main;

			if (summary.has_value())
			{
				main = summary->Main;
			}
			else
			{
				for (const Turn& turn : session.GetTurns())
					for (const auto& [key, counts] : turn.Usage)
						main[key] += counts;
			}

			const CostResult own = Sum(pricing, main);
			const CostResult subagents = summary.has_value() ? Sum(pricing, summary->Subagents) : CostResult{};

			CostResult total = own;
			total.Dollars += subagents.Dollars;
			total.UnknownModels.insert(total.UnknownModels.end(), subagents.UnknownModels.begin(),
									   subagents.UnknownModels.end());

			ImGui::Text("Total %s   session %s   subagents %s", FormatCost(total).c_str(), FormatCost(own).c_str(),
						FormatCost(subagents).c_str());

			if (!total.IsKnown())
			{
				std::string names;
				for (const std::string& model : total.UnknownModels)
					names += (names.empty() ? "" : ", ") + model;

				ImGui::TextDisabled("No price for %s. Add one under Settings > Claude.", names.c_str());
			}

			if (!ImGui::BeginTable("##models", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
				return;

			ImGui::TableSetupColumn("Model");
			ImGui::TableSetupColumn("Tokens");
			ImGui::TableSetupColumn("Cost");
			ImGui::TableSetupColumn("Share");
			ImGui::TableHeadersRow();

			UsageByModel all = main;
			if (summary.has_value())
				for (const auto& [key, counts] : summary->Subagents)
					all[key] += counts;

			for (const auto& [key, counts] : all)
			{
				const CostResult cost = pricing.CostOf({{key, counts}});

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s%s", key.Model.c_str(), key.Fast ? " (fast)" : "");
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(counts.Total()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatCost(cost).c_str());
				ImGui::TableNextColumn();

				if (cost.IsKnown() && total.IsKnown() && total.Dollars > 0.0)
					ImGui::Text("%.0f%%", 100.0 * cost.Dollars / total.Dollars);
			}

			ImGui::EndTable();
		}

		void DrawChart(const std::vector<BucketCosts>& turns)
		{
			if (turns.empty())
				return;

			constexpr int kBuckets = 4;
			const char* labels[kBuckets] = {"Input", "Output", "Cache write", "Cache read"};

			// Item-major, as PlotBarGroups reads it: every turn's input, then every turn's output...
			std::vector<double> values(static_cast<std::size_t>(kBuckets) * turns.size());

			for (std::size_t turn = 0; turn < turns.size(); ++turn)
			{
				values[0 * turns.size() + turn] = turns[turn].Input;
				values[1 * turns.size() + turn] = turns[turn].Output;
				values[2 * turns.size() + turn] = turns[turn].CacheWrite;
				values[3 * turns.size() + turn] = turns[turn].CacheRead;
			}

			if (ImPlot::BeginPlot("Cost per turn", ImVec2(-1.0f, ImGui::GetFontSize() * 16.0f)))
			{
				ImPlot::SetupAxes("Turn", "Dollars", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
				ImPlot::SetupLegend(ImPlotLocation_NorthWest);
				ImPlot::PlotBarGroups(labels, values.data(), kBuckets, static_cast<int>(turns.size()), 0.67, 1.0,
									  ImPlotBarGroupsFlags_Stacked);
				ImPlot::EndPlot();
			}
		}

		void DrawTurnTable(const Session& session, const std::vector<BucketCosts>& turns)
		{
			if (!ImGui::BeginTable("##turns", 7,
								   ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			for (const char* column : {"#", "Prompt", "Input", "Output", "Cache write", "Cache read", "Cost"})
				ImGui::TableSetupColumn(column);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(turns.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const BucketCosts& cost = turns[static_cast<std::size_t>(row)];
					const Turn& turn = session.GetTurns()[static_cast<std::size_t>(row)];

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%d", row + 1);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(TruncateUtf8(turn.Prompt, 80).c_str());

					for (const double dollars : {cost.Input, cost.Output, cost.CacheWrite, cost.CacheRead, cost.Total()})
					{
						ImGui::TableNextColumn();
						ImGui::Text("$%.3f", dollars);
					}
				}
			}

			ImGui::EndTable();
		}
	}

	void DrawCost(const Session& session, const std::optional<SessionSummary>& summary, const Pricing& pricing)
	{
		const std::vector<BucketCosts> turns = TurnCosts(session, pricing);

		DrawTotals(session, summary, pricing);
		ImGui::Spacing();
		DrawChart(turns);
		ImGui::Spacing();
		DrawTurnTable(session, turns);
	}
}

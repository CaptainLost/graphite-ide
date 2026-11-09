#include "Search/CommandPalette.h"

#include "Search/QuickOpenMatch.h"
#include "UI/Lists/ListRow.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/TextInput.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		constexpr float kWidth = 640.0f;
		constexpr float kTopGap = 48.0f;
		constexpr std::size_t kVisibleRows = 14;

		// Every command fits; the limit only keeps a runaway registry from drawing thousands of rows.
		constexpr std::size_t kShownMatches = 500;

		std::string PaletteLabelOf(const Core::Command& command)
		{
			const std::string& label = command.ListLabel.empty() ? command.Label : command.ListLabel;
			return label.substr(0, label.find('\t'));
		}

		std::string ShortcutOf(const Core::Binding* binding)
		{
			if (binding == nullptr || binding->Primary.IsEmpty())
			{
				return {};
			}

			return binding->Primary.ToString();
		}
	}

	std::vector<PaletteEntry> ListPaletteEntries(const Core::CommandRegistry& commands, const Core::Keymap& keys,
												 std::string_view leaveOut)
	{
		std::vector<PaletteEntry> entries;

		commands.ForEach([&](const Core::Command& command) {
			if (command.Id == leaveOut || (!command.Run && !command.RunFromMenu) || !Core::IsCommandEnabled(command))
			{
				return;
			}

			std::string label = PaletteLabelOf(command);

			if (label.empty())
			{
				return;
			}

			entries.push_back(PaletteEntry{.Id = command.Id, .Label = std::move(label), .Shortcut = ShortcutOf(keys.For(command.Id))});
		});

		std::ranges::stable_sort(entries, [](const PaletteEntry& left, const PaletteEntry& right) { return left.Label < right.Label; });

		return entries;
	}

	std::vector<std::size_t> RankPaletteEntries(std::string_view typed, const std::vector<PaletteEntry>& entries)
	{
		std::vector<std::string> labels;
		labels.reserve(entries.size());

		for (const PaletteEntry& entry : entries)
		{
			labels.push_back(entry.Label);
		}

		return RankQuickOpenMatches(typed, labels, kShownMatches);
	}

	CommandPalette::CommandPalette(RunCommand run)
		: m_Run(std::move(run))
	{
	}

	void CommandPalette::Open(std::vector<PaletteEntry> entries)
	{
		m_Open = true;
		m_JustOpened = true;
		m_Typed.clear();
		m_Entries = std::move(entries);
		m_Matches.clear();
		m_Selected = 0;
		m_RankedFor.clear();
		m_Ranked = false;
	}

	void CommandPalette::Rank()
	{
		if (m_Ranked && m_RankedFor == m_Typed)
		{
			return;
		}

		m_Matches = RankPaletteEntries(m_Typed, m_Entries);
		m_RankedFor = m_Typed;
		m_Ranked = true;
		m_Selected = m_Matches.empty() ? 0 : std::min(m_Selected, m_Matches.size() - 1);
	}

	void CommandPalette::Render()
	{
		constexpr const char* kPopupId = "##CommandPalette";

		if (!m_Open)
		{
			return;
		}

		if (m_JustOpened)
		{
			ImGui::OpenPopup(kPopupId);
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float width = std::min(Core::Scaled(kWidth), viewport->WorkSize.x - Core::Scaled(kTopGap));
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f,
									   viewport->WorkPos.y + Core::Scaled(kTopGap)));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));

		if (!ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			// ImGui closed it: a click outside, or Escape.
			if (!m_JustOpened)
			{
				m_Open = false;
			}

			return;
		}

		Rank();

		if (m_JustOpened)
		{
			ImGui::SetKeyboardFocusHere();
			m_JustOpened = false;
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered = Core::InputTextWithHint("##commandPaletteQuery", "Run a command by name", m_Typed,
													 ImGuiInputTextFlags_EnterReturnsTrue);

		if (ImGui::IsItemEdited())
		{
			m_Selected = 0;
		}

		m_SelectionMoved = false;

		if (!m_Matches.empty())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			{
				m_Selected = (m_Selected + 1) % m_Matches.size();
				m_SelectionMoved = true;
			}

			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			{
				m_Selected = (m_Selected + m_Matches.size() - 1) % m_Matches.size();
				m_SelectionMoved = true;
			}
		}

		std::optional<std::size_t> chosen;

		if (entered && !m_Matches.empty())
		{
			chosen = m_Matches[m_Selected];
		}

		// A row is its line and the spacing ListBodyScope keeps under it.
		const float rowHeight = ListRowLineHeight() + ImGui::GetStyle().ItemSpacing.y;
		const std::size_t rows = std::min(std::max<std::size_t>(m_Matches.size(), 1), kVisibleRows);
		const float listHeight = rowHeight * static_cast<float>(rows) + ImGui::GetStyle().WindowPadding.y;

		if (ImGui::BeginChild("##commandPaletteMatches", ImVec2(0.0f, listHeight)))
		{
			const ListBodyScope listBody;

			if (m_Matches.empty())
			{
				ListRowSelectable("##nothing", false, ImGuiSelectableFlags_Disabled);
				ImGui::SameLine(ImGui::GetStyle().FramePadding.x);
				ImGui::TextDisabled("No command matches");
			}

			const float rowRight = ImGui::GetContentRegionAvail().x;

			for (std::size_t row = 0; row < m_Matches.size(); ++row)
			{
				const PaletteEntry& entry = m_Entries[m_Matches[row]];

				ImGui::PushID(static_cast<int>(row));
				const bool selected = row == m_Selected;

				if (ListRowSelectable("##command", selected, ImGuiSelectableFlags_AllowOverlap))
				{
					chosen = m_Matches[row];
				}

				if (selected && m_SelectionMoved)
				{
					ImGui::SetScrollHereY();
				}

				ImGui::SameLine(ImGui::GetStyle().FramePadding.x);
				ImGui::TextUnformatted(entry.Label.c_str());

				if (!entry.Shortcut.empty())
				{
					const float shortcutWidth = ImGui::CalcTextSize(entry.Shortcut.c_str()).x;
					const float labelEnd = ImGui::GetStyle().FramePadding.x + ImGui::CalcTextSize(entry.Label.c_str()).x +
										   ImGui::GetStyle().ItemSpacing.x;
					ImGui::SameLine(std::max(rowRight - shortcutWidth - ImGui::GetStyle().FramePadding.x, labelEnd));
					ImGui::TextDisabled("%s", entry.Shortcut.c_str());
				}

				ImGui::PopID();
			}
		}

		ImGui::EndChild();

		if (chosen.has_value())
		{
			const std::string id = m_Entries[*chosen].Id;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			m_Open = false;
			m_Run(id);
			return;
		}

		ImGui::EndPopup();
	}
}

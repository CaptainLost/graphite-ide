#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/LoadingState.h"

#include "Git/Branches/RefSectionHeading.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"
#include "Git/StashBranch.h"
#include "UI/ClipboardCopy.h"
#include "UI/FittedText.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace Graphite {

	namespace {

		void FittedLine(const std::string& text, bool isDimmed)
		{
			const std::string shown = FitToLine(text);

			if (isDimmed)
			{
				ImGui::TextDisabled("%s", shown.c_str());
			}
			else
			{
				ImGui::TextUnformatted(shown.c_str());
			}
		}

		const Core::StashEntry* FindStash(const std::vector<Core::StashEntry>* stashes, const Core::Oid& stashId)
		{
			if (stashes == nullptr)
			{
				return nullptr;
			}

			const std::vector<Core::StashEntry>::const_iterator found = std::ranges::find(*stashes, stashId, &Core::StashEntry::Id);
			return found == stashes->end() ? nullptr : &*found;
		}
	}

	void GitView::RenderStashes()
	{
		const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();
		const std::optional<Core::Error>& error = m_Model.GetStashesError();

		const bool stashListWasRead = static_cast<bool>(stashes);
		const bool selectedStashIsGone =
			m_SelectedStash.has_value() && stashListWasRead && FindStash(stashes.get(), *m_SelectedStash) == nullptr;

		if (selectedStashIsGone)
		{
			DeselectStash();
			RequestLog(HistoryReach{});
		}

		const std::optional<std::size_t> stashCount =
			stashListWasRead ? std::optional<std::size_t>{stashes->size()} : std::nullopt;

		if (!DrawRefSectionHeading("Stashes", "Stashes", StashSectionCount(stashCount), m_StashesCollapsed))
		{
			return;
		}

		if (error.has_value())
		{
			ImGui::TextDisabled("%s", error->Message.c_str());
		}

		if (!stashListWasRead)
		{
			if (!error.has_value())
			{
				ImGui::PushID("##stashesLoading");
				Core::LoadingLine("Loading stashes");
				ImGui::PopID();
			}

			return;
		}

		if (stashes->empty())
		{
			if (!error.has_value())
			{
				ImGui::TextDisabled("No stashes");
			}

			return;
		}

		const std::chrono::time_zone* const zone = ReaderTimeZone();

		for (const Core::StashEntry& stash : *stashes)
		{
			// WARNING: The id is the stash id, not the row index, so a menu left open while another stash is pushed stays on its own stash.
			ImGui::PushID(stash.Id.ToHex().c_str());

			const std::chrono::sys_seconds madeAt{std::chrono::seconds{stash.SecondsSinceEpoch}};
			const std::string when = FormattedTimestamp(madeAt, zone);

			// WARNING: The label is drawn over an id-only Selectable because a stash message is free text and a "##" in it would cut a Selectable label short.
			const ImVec2 rowStart = ImGui::GetCursorPos();

			const bool shown = m_Selection.View == GitWindowView::History && m_SelectedStash == stash.Id;

			if (ListRowSelectable("##stash", shown, ImGuiSelectableFlags_AllowOverlap))
			{
				SelectStash(stash.Id);
			}

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s\n%s", stash.Message.c_str(), when.c_str());
			}

			RenderStashMenu(stash);

			ImGui::SetCursorPos(ImVec2(rowStart.x, ListRowTextTop(rowStart.y)));

			const StashLabel label = StashRowLabel(stash.Message, when);
			FittedLine(label.Text, !label.Named);

			ImGui::PopID();
		}
	}

	void GitView::RenderStashMenu(const Core::StashEntry& stash)
	{
		const ListPopupScope popup;

		if (!Core::BeginRightClickPopup("stash-menu"))
		{
			return;
		}

		ImGui::BeginDisabled(m_Model.IsWriteUnsettled());

		if (ImGui::MenuItem("Apply"))
		{
			m_Model.RequestStashApply(stash.Id);
		}

		if (ImGui::MenuItem("Pop"))
		{
			m_Model.RequestStashPop(stash.Id);
		}

		if (ImGui::MenuItem("Drop"))
		{
			m_DropStashQuestion.AskOnce(DropStashPayload{.Index = stash.Index, .Id = stash.Id, .Message = stash.Message});
		}

		ImGui::EndDisabled();

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Message"))
		{
			CopyToClipboard(m_Events, "stash message", stash.Message);
		}

		if (ImGui::MenuItem("Copy Hash"))
		{
			CopyToClipboard(m_Events, "stash hash", stash.Id.ToHex());
		}

		ImGui::EndPopup();
	}

	void GitView::SelectStash(const Core::Oid& stash)
	{
		m_PendingRestore.reset();
		m_SelectedStash = stash;
		m_Selection = {GitWindowView::History, m_SelectedRef.value_or(std::string{})};

		RequestLog(HistoryReach{});
	}

	void GitView::DeselectStash()
	{
		m_SelectedStash.reset();
	}
}

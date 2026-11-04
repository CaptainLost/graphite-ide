#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "UI/ClipboardCopy.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace Graphite {

	namespace {

		bool HoldsStash(const std::vector<Core::StashEntry>& stashes, const Core::Oid& stashId)
		{
			return std::ranges::find(stashes, stashId, &Core::StashEntry::Id) != stashes.end();
		}
	}

	void GitView::FollowSelectedStash()
	{
		const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();

		if (!m_SelectedStash.has_value() || !stashes || HoldsStash(*stashes, *m_SelectedStash))
		{
			return;
		}

		DeselectStash();
		RequestLog(HistoryReach{});
	}

	void GitView::RenderStashMenuItems(const Core::StashEntry& stash)
	{
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
	}

	void GitView::SelectStash(const Core::Oid& stash)
	{
		m_PendingRestore.reset();
		m_SelectedStash = stash;
		m_OpeningRef = false;

		RequestLog(HistoryReach{});
	}

	void GitView::DeselectStash()
	{
		m_SelectedStash.reset();
	}
}

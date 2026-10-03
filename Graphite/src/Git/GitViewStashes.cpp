// The stash section of the refs column. Split out of GitView.cpp for the same reason the
// dialogs are: a part of the panel with its own state and its own drawing.

#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/ContextMenu.h"

#include "Git/StashBranch.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>

namespace Graphite {

	namespace {

		// FitToLine's text, dimmed when `dimmed`.
		void FittedLine(const std::string& text, bool dimmed)
		{
			const std::string shown = FitToLine(text);

			if (dimmed)
			{
				ImGui::TextDisabled("%s", shown.c_str());
			}
			else
			{
				ImGui::TextUnformatted(shown.c_str());
			}
		}

		const Core::StashEntry* FindStash(const std::vector<Core::StashEntry>* stashes, const Core::Oid& id)
		{
			if (stashes == nullptr)
			{
				return nullptr;
			}

			const auto found = std::ranges::find(*stashes, id, &Core::StashEntry::Id);
			return found == stashes->end() ? nullptr : &*found;
		}
	}

	void GitView::RenderStashes()
	{
		const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();
		const std::optional<Core::Error>& error = m_Model.GetStashesError();

		// A stash popped or dropped - here or elsewhere - gives the history back to the
		// branch, the way a deleted branch gives it back to HEAD. Only against a list that was
		// read: one that failed says nothing about what is stashed.
		if (m_SelectedStash.has_value() && stashes && FindStash(stashes.get(), *m_SelectedStash) == nullptr)
		{
			DeselectStash();
			RequestLog(std::nullopt);
		}

		if (!RenderRefSection("Stashes", stashes ? stashes->size() : 0, RefSection::Stashes))
		{
			return;
		}

		// A list that could not be read says why rather than claiming there is nothing - and
		// above an older list too, which may no longer be what is stashed.
		if (error.has_value())
		{
			ImGui::TextDisabled("%s", error->Message.c_str());
		}

		if (!stashes || stashes->empty())
		{
			if (!error.has_value())
			{
				ImGui::TextDisabled("No stashes");
			}

			return;
		}

		// Once for the list rather than per row: it is a time zone lookup.
		const std::chrono::minutes offset = LocalOffset();

		for (const Core::StashEntry& stash : *stashes)
		{
			// By id rather than by index, so a menu left open while a stash is pushed
			// elsewhere still belongs to the stash it was opened on.
			ImGui::PushID(stash.Id.ToHex().c_str());

			const std::chrono::sys_seconds made{std::chrono::seconds{stash.Time}};
			const std::string when = std::format("{:%Y-%m-%d %H:%M}", made + offset);

			// The label is laid over an id-only Selectable rather than passed as one: a stash
			// message is free text, and a "##" in it would cut the label short.
			const ImVec2 rowStart = ImGui::GetCursorPos();

			if (ImGui::Selectable("##stash", m_SelectedStash == stash.Id, ImGuiSelectableFlags_AllowOverlap))
			{
				SelectStash(stash.Id);
			}

			// The whole message as git recorded it, which the row cuts down to the reader's
			// part of it - or leaves out, for a stash made without one.
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s\n%s", stash.Message.c_str(), when.c_str());
			}

			RenderStashMenu(stash);

			ImGui::SetCursorPos(rowStart);

			const StashLabel label = StashRowLabel(stash.Message, when);
			FittedLine(label.Text, !label.Named);

			ImGui::PopID();
		}
	}

	void GitView::RenderStashMenu(const Core::StashEntry& stash)
	{
		if (!Core::BeginRightClickPopup("stash-menu"))
		{
			return;
		}

		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

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
			AskOnce(m_DropStashQuestion, DropStashPayload{.Index = stash.Index, .Id = stash.Id, .Message = stash.Message});
		}

		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	void GitView::SelectStash(const Core::Oid& stash)
	{
		m_SelectedStash = stash;

		// Through RequestLog for the reason picking a branch goes through it: the filter in
		// the boxes still applies. The commit list keeps whatever selection the new page
		// still lists, as it does for any page.
		RequestLog(std::nullopt);
	}

	void GitView::DeselectStash()
	{
		m_SelectedStash.reset();
	}
}

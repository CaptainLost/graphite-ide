// The stash section of the refs column, and the pane below the history while a stash is
// selected. Split out of GitView.cpp for the same reason the dialogs are: a part of the
// panel with its own state and its own drawing.

#include "Git/GitView.h"

#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"

#include "Git/CommitDetails.h"
#include "Git/CommitLabels.h"
#include "Git/StashBranch.h"
#include "UI/HighlightedRow.h"
#include "UI/SplitResults.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

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
		const std::shared_ptr<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();
		const std::optional<Core::Error>& error = m_Model.GetStashesError();

		// A stash popped or dropped - here or elsewhere - takes its preview with it. Only
		// against a list that was read: one that failed says nothing about what is stashed.
		if (m_SelectedStash.has_value() && stashes && FindStash(stashes.get(), *m_SelectedStash) == nullptr)
		{
			DeselectStash();
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
			AskOnce(m_DropStashQuestion, DropStashPayload{.Index = stash.Index, .Id = stash.Id});
		}

		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	void GitView::SelectStash(const Core::Oid& stash)
	{
		// The same stash clicked again starts it over too: back to its file list, and a
		// diff that failed asked for again.
		DeselectStash();

		m_SelectedStash = stash;
		m_SelectedStashFile.reset();

		m_Commits.ClearSelection();
		m_Commits.ClearCommitSelection();
	}

	void GitView::DeselectStash()
	{
		// A diff that failed is forgotten as the reader leaves it, so selecting the stash again
		// asks again - the recourse the commit list gives a row by collapsing it.
		if (m_SelectedStash.has_value())
		{
			m_Model.ForgetFailedDiffFor(*m_SelectedStash);
		}

		m_SelectedStash.reset();
		m_SelectedStashFile.reset();
	}

	void GitView::RenderStashPreview(const GitPalette& palette)
	{
		const std::shared_ptr<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();
		const Core::StashEntry* stash = FindStash(stashes.get(), *m_SelectedStash);

		if (stash == nullptr)
		{
			// The list could not be read again, so whether the stash is still there is not
			// known - and with it gone from view, nothing here can be described.
			m_Diff.OnOpenAsTab = nullptr;
			m_Diff.OnClose = nullptr;
			m_Diff.OnOpenFile = nullptr;
			Core::EmptyState("That stash is not in the list any more.");
			return;
		}

		// A stash is a commit whose first parent is where it was made, so the diff against
		// that parent is what it holds - the same query a commit row makes. Asked every frame,
		// which costs nothing once held, in flight or failed.
		if (!m_Model.GetDiffFor(stash->Id))
		{
			m_Model.RequestDiffFor(stash->Id);
		}

		if (m_SelectedStashFile.has_value())
		{
			RenderFileDiff(FileSelection{stash->Id, *m_SelectedStashFile},
						   [this] { m_SelectedStashFile.reset(); }, palette);
			return;
		}

		m_Diff.OnOpenAsTab = nullptr;
		m_Diff.OnClose = nullptr;
		m_Diff.OnOpenFile = nullptr;

		const std::chrono::sys_seconds made{std::chrono::seconds{stash->Time}};
		const std::string when = std::format("{:%Y-%m-%d %H:%M}", made + LocalOffset());
		const StashLabel label = StashRowLabel(stash->Message, when);

		// Wrapped short of the close below, which places itself at the right corner of this
		// line whatever is drawn there: the pane is wide enough to say the whole label, and a
		// first line run under the button would lose its end.
		const float closeWidth = ImGui::CalcTextSize(ICON_MDI_CLOSE).x + ImGui::GetStyle().FramePadding.x * 2.0f +
								 ImGui::GetStyle().ItemSpacing.x;

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - closeWidth);

		if (label.Named)
		{
			ImGui::TextUnformatted(label.Text.c_str());
		}
		else
		{
			ImGui::TextDisabled("%s", label.Text.c_str());
		}

		ImGui::PopTextWrapPos();

		// On the label's own line, at the pane's right corner, where the commit details and
		// the diff put theirs.
		if (DrawPaneClose())
		{
			DeselectStash();
			return;
		}

		// Wrapped: the message is git's whole line, which the label above cut down.
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", stash->Message.c_str());
		ImGui::PopTextWrapPos();

		ImGui::TextDisabled("%s  %s", when.c_str(), stash->Id.ShortHex().c_str());

		const std::shared_ptr<const Core::Diff> diff = m_Model.GetDiffFor(stash->Id);

		if (diff)
		{
			const DiffStats stats = CountChanges(*diff);

			ImGui::Text("%zu %s", stats.Files, stats.Files == 1 ? "file" : "files");
			ImGui::SameLine();
			ImGui::TextColored(ImColor(palette.GutterAdded), "+%zu", stats.Added);
			ImGui::SameLine();
			ImGui::TextColored(ImColor(palette.GutterRemoved), "-%zu", stats.Removed);
		}
		else if (m_Model.IsDiffFailed(stash->Id))
		{
			ImGui::TextDisabled("Could not read what this stash holds.");
		}
		else
		{
			// One line tall either way, so the pane does not jump when the diff lands.
			ImGui::TextDisabled("counting changes…");
		}

		ImGui::Separator();

		if (!diff)
		{
			return;
		}

		for (const Core::FileDiff& file : diff->Files)
		{
			const std::filesystem::path& path = ShownPath(file);

			ImGui::PushID(Core::ToUtf8(path).c_str());

			const std::string letter(1, ChangeLetter(file.Change));

			if (MarkedSelectable("##file", false, letter, ChangeColor(file.Change, palette),
								 std::format("  {}", Core::ToUtf8(path))))
			{
				m_SelectedStashFile = path;
			}

			ImGui::PopID();
		}
	}
}

#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"

#include "Git/DefaultBranch.h"
#include "Git/GitStatusText.h"
#include "Git/History/CommitLabels.h"
#include "Git/Window/ChipRow.h"
#include "Git/Window/GitWindowRestore.h"
#include "Theme/Palettes.h"
#include "UI/ClipboardCopy.h"
#include "UI/FittedText.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kChangesListWidthInFontSizes = 22.0f;
		constexpr int kChangesColumnCount = 2;

		// The header's path is in the code face, smaller than the folder's name beside it.
		constexpr float kHeaderPathScale = 0.8f;
		constexpr float kHistoryTitleScale = 0.9f;
		constexpr float kKeepFontSize = 0.0f;

		const std::vector<Core::Branch>& BranchesOrNone(const Core::Ref<const std::vector<Core::Branch>>& branches)
		{
			static const std::vector<Core::Branch> kNoBranches;
			return branches ? *branches : kNoBranches;
		}

		std::chrono::sys_seconds NowInSeconds()
		{
			return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
		}

		std::string FolderName(const std::filesystem::path& folder)
		{
			// libgit2 hands the working directory back with a trailing separator, whose filename is empty.
			const std::filesystem::path name = folder.has_filename() ? folder.filename() : folder.parent_path().filename();
			return Core::ToUtf8(name.empty() ? folder : name);
		}

		void EmphasisedLine(const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
		}

		std::optional<std::size_t> CheckedOutRefOf(const LineRead& read)
		{
			if (!read.Branches)
			{
				return std::nullopt;
			}

			const std::vector<Core::Branch>& branches = *read.Branches;
			const auto checkedOut = std::ranges::find(branches, true, &Core::Branch::IsHead);

			if (checkedOut == branches.end())
			{
				return std::nullopt;
			}

			return static_cast<std::size_t>(checkedOut - branches.begin());
		}
	}

	void GitView::ConnectViews()
	{
		m_Sidebar.OnClick = [this](const SidebarRow& row) { AnswerSidebarClick(row); };
		m_Sidebar.DrawRowMenu = [this](const SidebarRow& row) { DrawSidebarRowMenu(row); };

		m_Changes.OnFileChosen = [this](const OpenDiff& diff) {
			// WARNING: GitModel keeps a single slot for the working-tree diff, so a working-tree tab of another diff
			// would take turns with this one; the newest pick closes it, as the side panel's picks do.
			if (!diff.Commit.has_value() && !m_DiffWindows.IsOpen(diff))
			{
				m_DiffWindows.CloseWorkingTree();
			}

			m_ChangesDiff.Diff = diff;
			m_ChangesPicked = true;
		};
	}

	GitWindowState GitView::CaptureWindowState() const
	{
		// WARNING: A restore still waiting for the branches is what the window will show, not HEAD's history it shows
		// meanwhile.
		if (m_PendingRestore.has_value())
		{
			return *m_PendingRestore;
		}

		return GitWindowState{.Ref = m_SelectedRef.value_or(std::string{})};
	}

	void GitView::RestoreWindowState(const GitWindowState& state)
	{
		m_PendingRestore = state;
		ApplyPendingRestore();
	}

	void GitView::ApplyPendingRestore()
	{
		if (!m_PendingRestore.has_value())
		{
			return;
		}

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		if (!m_PendingRestore->Ref.empty() && !branches)
		{
			return;
		}

		const GitWindowState state = *m_PendingRestore;
		m_PendingRestore.reset();

		const std::optional<std::string> ref = RefToRestore(state, BranchesOrNone(branches));

		// HEAD's history, already what the window shows, is asked for once the history is first drawn.
		if (ref == m_SelectedRef && !m_SelectedStash.has_value())
		{
			return;
		}

		SelectRef(ref);
	}

	void GitView::RenderFolderHeader()
	{
		const float room = ImGui::GetContentRegionAvail().x;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;

		// The folder's name in bold and its path after it, on one line.
		if (const std::filesystem::path& folder = m_Model.GetWorkingDirectory(); !folder.empty())
		{
			const std::filesystem::path shown = Core::NormalizeDirectory(folder);
			const std::string path = Core::ToUtf8(shown);
			const float lineStart = ImGui::GetCursorScreenPos().x;

			ImGui::BeginGroup();
			ImGui::TextUnformatted(ICON_LC_FOLDER);
			ImGui::SameLine();

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			EmphasisedLine(FitToWidth(FolderName(shown), room - (ImGui::GetCursorScreenPos().x - lineStart)));
			ImGui::PopFont();

			const float pathRoom = room - (ImGui::GetItemRectMax().x + spacing - lineStart);

			if (pathRoom > 0.0f)
			{
				ImGui::SameLine();
				Core::PushCodeFont(ImGui::GetFontSize() * kHeaderPathScale);
				ImGui::TextDisabled("%s", FitToWidth(path, pathRoom).c_str());
				ImGui::PopFont();
			}

			ImGui::EndGroup();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s", path.c_str());
			}
		}

		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const Core::Ref<const Core::WorkingTreeStatus> status = m_Model.GetStatus();

		const std::optional<GitStatusSegment> segment = GitStatusText(GitStatusInputs{
			.FolderOpen = true,
			.Head = head.get(),
			.Branches = branches.get(),
			.Status = status.get(),
			.CheckingOut = CurrentWait().CheckingOut});

		if (segment.has_value() && segment->BranchBusy)
		{
			// A checkout under way is named here in place of HEAD's branch, with the loader beside it.
			const std::string text = FitToWidth(segment->Branch, room - Core::LoadingLineWidth(""));

			ImGui::PushID("##checkingOut");
			Core::LoadingLine(text.c_str(), Core::WhileLoaderWaits::ShowText);
			ImGui::PopID();
		}
		else if (segment.has_value() && !segment->Branch.empty())
		{
			// HEAD's branch, then its distance to the upstream in a chip with the status bar's marks.
			const bool inSync = segment->Distance == ICON_LC_CHECK;
			const Chip distance{inSync ? std::string(ICON_LC_CHECK " in sync") : segment->Distance,
								inSync ? ChipTone::Good : ChipTone::Plain};
			const float chipRoom = segment->Distance.empty() ? 0.0f : ChipWidthOf(distance.Text) + spacing;

			ImGui::BeginGroup();
			ImGui::TextUnformatted(FitToWidth(segment->Branch, room - chipRoom).c_str());

			if (!segment->Distance.empty())
			{
				ImGui::SameLine();
				DrawChipRow({distance}, ReadChipColours(m_Scheme.Get()));
			}

			ImGui::EndGroup();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				const std::string tooltip = segment->DistanceTooltip.empty()
												? segment->BranchTooltip
												: segment->BranchTooltip + "\n" + segment->DistanceTooltip;
				ImGui::SetTooltip("%s", tooltip.c_str());
			}
		}
		else if (const std::optional<Core::Error>& headError = m_Model.GetHeadError(); headError.has_value())
		{
			ImGui::TextDisabled("%s", FitToWidth(headError->Message, room).c_str());
		}
		else
		{
			ImGui::PushID("##headLoading");
			Core::LoadingLine("Reading HEAD");
			ImGui::PopID();
		}

		ImGui::Separator();
	}

	void GitView::RenderSidebar()
	{
		RebuildSidebarWhenOutOfDate();

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const std::optional<SidebarWaitingRow> waiting = DescribeSidebarWait(SidebarWaitInput{
			.HasRead = m_SidebarBuiltFrom->Read != nullptr,
			.Reading = m_Lines.IsReading(),
			.LinesError = m_Lines.GetError(),
			.Branches = branches.get(),
			.BranchesError = m_Model.GetBranchesError()});

		const std::function<SidebarRowText(const SidebarRow&)> textOf = [this](const SidebarRow& row) {
			return SidebarTextOf(row);
		};

		m_Sidebar.Draw(&m_SidebarRows, textOf, waiting, SidebarSelectionNow(), ReadSidebarColours(m_Scheme.Get()));
	}

	void GitView::RebuildSidebarWhenOutOfDate()
	{
		SidebarInputs inputs{.Read = m_Lines.GetRead(),
							 .Stashes = m_Model.GetStashes(),
							 .Authors = m_Model.GetBranchAuthors(),
							 .Search = m_Sidebar.GetSearch(),
							 .ChoicesVersion = m_SidebarChoicesVersion,
							 .Now = std::chrono::floor<std::chrono::minutes>(NowInSeconds())};

		if (m_SidebarBuiltFrom == inputs)
		{
			return;
		}

		// WARNING: The lines view into the read they were built from, so they go before that read can.
		if (!m_SidebarBuiltFrom.has_value() || m_SidebarBuiltFrom->Read != inputs.Read)
		{
			m_SidebarLines.reset();
			m_MostCommonLine.clear();
			m_HeadRef.reset();

			if (inputs.Read)
			{
				m_SidebarLines.emplace(*inputs.Read);
				m_MostCommonLine = MostCommonLineOf(*inputs.Read);
				m_HeadRef = CheckedOutRefOf(*inputs.Read);
			}
		}

		if (inputs.Read)
		{
			m_SidebarRows = BuildSidebarRows(SidebarInput{.Read = *inputs.Read,
														  .Stashes = inputs.Stashes.get(),
														  .Authors = inputs.Authors.get(),
														  .Search = inputs.Search,
														  .OpenByUser = m_OpenByUser,
														  .ShowingMerged = m_ShowingMerged,
														  .Now = inputs.Now});
		}
		else
		{
			m_SidebarRows = BuildStashRows(inputs.Stashes.get(), inputs.Search, m_OpenByUser);
		}

		m_SidebarBuiltFrom = std::move(inputs);
	}

	const Core::Branch* GitView::SidebarBranchOf(const SidebarRow& row) const
	{
		const Core::Ref<const LineRead>& read = m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Read : nullptr;

		if (!read || !read->Branches || !row.Ref.has_value() || *row.Ref >= read->Branches->size())
		{
			return nullptr;
		}

		return &(*read->Branches)[*row.Ref];
	}

	const Core::StashEntry* GitView::SidebarStashOf(const SidebarRow& row) const
	{
		const Core::Ref<const std::vector<Core::StashEntry>>& stashes =
			m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Stashes : nullptr;

		if (!stashes || !row.Stash.has_value() || *row.Stash >= stashes->size())
		{
			return nullptr;
		}

		return &(*stashes)[*row.Stash];
	}

	SidebarRowText GitView::SidebarTextOf(const SidebarRow& row) const
	{
		if (row.Kind == SidebarRowKind::Stash)
		{
			const Core::StashEntry* stash = SidebarStashOf(row);
			return stash != nullptr ? DescribeStashRow(*stash, NowInSeconds(), ReaderTimeZone()) : SidebarRowText{};
		}

		if (!m_SidebarLines.has_value())
		{
			return SidebarRowText{};
		}

		return DescribeSidebarRow(row, SidebarTextInput{.Read = *m_SidebarBuiltFrom->Read,
														.Lines = *m_SidebarLines,
														.UserEmail = m_Model.GetUserEmail(),
														.MostCommonLine = m_MostCommonLine,
														.Now = NowInSeconds()});
	}

	SidebarSelection GitView::SidebarSelectionNow() const
	{
		SidebarSelection selection;

		if (m_SelectedStash.has_value())
		{
			const Core::Ref<const std::vector<Core::StashEntry>>& stashes =
				m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Stashes : nullptr;

			if (stashes)
			{
				const auto shown = std::ranges::find(*stashes, *m_SelectedStash, &Core::StashEntry::Id);

				if (shown != stashes->end())
				{
					selection.Stash = static_cast<std::size_t>(shown - stashes->begin());
				}
			}

			return selection;
		}

		if (m_SidebarLines.has_value())
		{
			selection.Ref = m_SelectedRef.has_value() ? m_SidebarLines->IndexOf(*m_SelectedRef) : m_HeadRef;
		}

		return selection;
	}

	void GitView::AnswerSidebarClick(const SidebarRow& row)
	{
		switch (row.Kind)
		{
		case SidebarRowKind::Branch:
		case SidebarRowKind::Base:
			if (const Core::Branch* branch = SidebarBranchOf(row))
			{
				SelectRef(branch->FullName);
			}
			return;
		case SidebarRowKind::Stash:
			if (const Core::StashEntry* stash = SidebarStashOf(row))
			{
				SelectStash(stash->Id);
			}
			return;
		case SidebarRowKind::Section:
		case SidebarRowKind::Group:
			m_OpenByUser[row.Key] = !row.Open;
			++m_SidebarChoicesVersion;
			return;
		case SidebarRowKind::Link:
			if (row.Link.has_value())
			{
				AnswerSidebarLink(*row.Link, row.Remote);
			}
			return;
		}
	}

	void GitView::AnswerSidebarLink(SidebarLink link, const std::string& remote)
	{
		switch (link)
		{
		case SidebarLink::ToYours:
			// Yours is the first section.
			m_OpenByUser[kYoursSectionKey] = true;
			++m_SidebarChoicesVersion;
			m_Sidebar.ScrollToTop();
			return;
		case SidebarLink::ShowMerged:
			m_ShowingMerged.insert(remote);
			++m_SidebarChoicesVersion;
			return;
		case SidebarLink::HideMerged:
			m_ShowingMerged.erase(remote);
			++m_SidebarChoicesVersion;
			return;
		case SidebarLink::WhyBases:
			// Answered by the explanation of the vs picker, which this window does not have yet.
			return;
		}
	}

	void GitView::DrawSidebarRowMenu(const SidebarRow& row)
	{
		const bool isStash = row.Kind == SidebarRowKind::Stash;
		const Core::Branch* branch = isStash ? nullptr : SidebarBranchOf(row);
		const Core::StashEntry* stash = isStash ? SidebarStashOf(row) : nullptr;

		std::string shownFor;

		if (branch != nullptr)
		{
			shownFor = branch->FullName;
		}
		else if (stash != nullptr)
		{
			shownFor = stash->Id.ToHex();
		}

		// WARNING: The sidebar keys a row by its place in the read or the stash list, which a branch or stash made or
		// gone shifts, so a menu left open while the rows are rebuilt could come to stand on another reference. It
		// closes instead of offering that one's actions under the one the reader right-clicked.
		if (ImGui::IsWindowAppearing())
		{
			m_SidebarMenuFor = shownFor;
		}
		else if (shownFor.empty() || shownFor != m_SidebarMenuFor)
		{
			ImGui::CloseCurrentPopup();
			return;
		}

		if (branch != nullptr)
		{
			RenderRefMenuItems(*branch);
		}
		else if (stash != nullptr)
		{
			RenderStashMenuItems(*stash);
		}
	}

	void GitView::RenderChanges(const GitPalette& palette)
	{
		m_ChangesDrawn = true;

		// The picked file's diff sits beside the list, which keeps the commit box at its foot.
		if (ImGui::BeginTable("gitChanges", kChangesColumnCount, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthFixed,
									ImGui::GetFontSize() * kChangesListWidthInFontSizes);
			ImGui::TableSetupColumn("diff", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			m_Changes.Draw();

			ImGui::TableSetColumnIndex(1);
			RenderChangesDiff(palette);

			ImGui::EndTable();
		}
	}

	void GitView::RenderChangesDiff(const GitPalette& palette)
	{
		if (!m_ChangesPicked)
		{
			Core::EmptyState("Pick a file to see its changes");
			return;
		}

		OpenDiff& diff = m_ChangesDiff.Diff;
		const Core::Ref<const Core::WorkingTreeStatus> status = m_Model.GetStatus();

		// A file staged or unstaged since it was picked is followed to the side that now holds it, as a diff tab is.
		const auto followed = [&status](OpenDiff working) {
			if (status && !working.Commit.has_value())
			{
				working.Target = TargetHolding(*status, working.Path, working.Target).value_or(working.Target);
			}

			return working;
		};

		diff = followed(diff);

		// WARNING: A tab of another working-tree diff opened since the pick is the newest, and it keeps GitModel's
		// single working-tree slot. The tab is followed here too, so the two agree whichever is drawn first.
		if (const std::optional<OpenDiff> tab = m_DiffWindows.WorkingTreeDiff();
			!diff.Commit.has_value() && tab.has_value() && followed(*tab) != diff)
		{
			m_ChangesPicked = false;
			Core::EmptyState("Pick a file to see its changes");
			return;
		}

		const SyntaxPalette syntax = ReadSyntaxPalette(m_Scheme.Get());

		const DiffBodyStyle style{
			.Palette = palette,
			.Syntax = syntax,
			.Selection = ReadEditorPalette(m_Scheme.Get()).Selection,
			.Options = m_EditorSettings.GetDiffViewOptions(),
			.OpenFile = m_DiffWindows.OpenFileIn(m_Model),
			.OnCopied = [this](std::string_view text) { ReportCopied(m_Events, "selection", text); },
			.OnSetOptions = [this](const DiffViewOptions& options) { m_EditorSettings.SetDiffViewOptions(options); }};

		DrawDiffBody(m_ChangesDiff, m_Model, style);
	}

	std::string GitView::HistoryTitle() const
	{
		if (m_SelectedStash.has_value())
		{
			const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();

			if (stashes)
			{
				const auto found = std::ranges::find(*stashes, *m_SelectedStash, &Core::StashEntry::Id);

				if (found != stashes->end() && !found->Message.empty())
				{
					return "Stash \xC2\xB7 " + found->Message;
				}
			}

			return "Stash";
		}

		if (m_SelectedRef.has_value())
		{
			const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
			const Core::Branch* branch = BranchWithFullName(BranchesOrNone(branches), *m_SelectedRef);

			return branch != nullptr ? branch->Name : Core::ShortBranchName(*m_SelectedRef);
		}

		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();

		if (!head)
		{
			return "HEAD";
		}

		switch (head->Kind)
		{
		case Core::HeadKind::Branch:
		case Core::HeadKind::Unborn:
			return "HEAD \xC2\xB7 " + head->BranchName;
		case Core::HeadKind::Detached:
			return "HEAD \xC2\xB7 " + (head->Commit.has_value() ? head->Commit->ShortHex() : std::string{}) + " (detached)";
		}

		return "HEAD";
	}

	void GitView::RenderHistoryHeader()
	{
		// A branch's history is named in the code face after the branch icon, a stash's after the archive's.
		ImGui::BeginGroup();
		ImGui::TextUnformatted(m_SelectedStash.has_value() ? ICON_LC_ARCHIVE : ICON_LC_GIT_BRANCH);
		ImGui::SameLine();

		Core::PushCodeFont(ImGui::GetFontSize() * kHistoryTitleScale);
		EmphasisedLine(FitToWidth(HistoryTitle(), ImGui::GetContentRegionAvail().x));
		ImGui::PopFont();
		ImGui::EndGroup();

		if (m_SelectedStash.has_value())
		{
			return;
		}

		// The branch the header names has its menu here too: the selected ref's, or HEAD's for HEAD's history.
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const std::vector<Core::Branch>& listed = BranchesOrNone(branches);
		const Core::Branch* const named =
			m_SelectedRef.has_value() ? BranchWithFullName(listed, *m_SelectedRef) : CheckedOutBranch(listed);

		// Keyed by the branch, so a menu left open while HEAD moves elsewhere closes rather than naming the new branch.
		if (named != nullptr)
		{
			ImGui::PushID(named->FullName.c_str());
			RenderRefMenu(*named);
			ImGui::PopID();
		}
	}
}

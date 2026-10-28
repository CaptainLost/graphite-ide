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
#include "Core/Platform/Shell.h"

#include "Git/DefaultBranch.h"
#include "Git/GitStatusText.h"
#include "Git/History/CommitLabels.h"
#include "Git/Window/GitWindowRestore.h"
#include "Git/Window/ThreadHeader.h"
#include "Theme/Palettes.h"
#include "UI/ClipboardCopy.h"
#include "UI/FittedText.h"
#include "UI/Lists/ListRow.h"
#include "UI/ToastEvent.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kChangesListWidthInFontSizes = 22.0f;
		constexpr int kChangesColumnCount = 2;

		constexpr const char* kNoTrunkName = "the trunk";

		// The header's path is in the code face, smaller than the folder's name beside it.
		constexpr float kHeaderPathScale = 0.8f;
		constexpr float kKeepFontSize = 0.0f;

		// The ref every stash is kept under: while a stash's history is shown, no branch row reads as shown.
		constexpr const char* kStashRef = "refs/stash";

		const std::vector<Core::Branch>& BranchesOrNone(const Core::Ref<const std::vector<Core::Branch>>& branches)
		{
			static const std::vector<Core::Branch> kNoBranches;
			return branches ? *branches : kNoBranches;
		}

		std::chrono::sys_seconds NowInSeconds()
		{
			return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
		}

		std::optional<std::size_t> ChangeCountOf(const Core::Ref<const Core::WorkingTreeStatus>& status)
		{
			if (!status)
			{
				return std::nullopt;
			}

			return status->Entries.size();
		}

		std::set<std::string> BranchesHeldByLinkedWorktrees(const WorkFacts* facts)
		{
			std::set<std::string> held;

			if (facts == nullptr)
			{
				return held;
			}

			for (const WorktreeFacts& worktree : facts->Worktrees)
			{
				const Core::WorktreeEntry& entry = worktree.Entry;

				if (!entry.IsMain && !entry.FolderMissing && entry.Head.Kind == Core::HeadKind::Branch)
				{
					held.insert(Core::LocalBranchReference(entry.Head.BranchName));
				}
			}

			return held;
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
	}

	void GitView::ConnectViews()
	{
		m_Navigator.OnSelect = [this](const GitWindowSelection& selection) { Select(selection); };
		m_Navigator.OnShowReady = [this] { ShowWork(true); };
		m_Navigator.DrawBranchMenu = [this](const Core::Branch& branch) { RenderRefMenu(branch); };

		m_WorkView.OnReview = [this](const WorkCard& card) { OpenThread(card.Branch); };
		m_WorkView.OnChanges = [this] { Select(GitWindowSelection{GitWindowView::Changes, {}}); };

		m_WorkView.OnOpenFolder = [this](const std::filesystem::path& folder) {
			if (const Core::Result<void> opened = Core::Shell::OpenFolder(folder); !opened)
			{
				m_Events.Publish(FolderToast(Core::ToastKind::Error, "Cannot open folder", opened.GetError().Message,
											 m_Model.GetWorkingDirectory()));
			}
		};

		m_WorkView.DrawBranchMenu = [this](const std::string& branchFullName) {
			const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

			if (const Core::Branch* branch = BranchWithFullName(BranchesOrNone(branches), branchFullName))
			{
				RenderRefMenuItems(*branch);
			}
		};

		m_WorkView.ReviewProgressOf = [this](const std::string& branchFullName) {
			return ReviewProgressOf ? ReviewProgressOf(branchFullName) : std::optional<ViewedCount>{};
		};

		m_WorkView.OnRefresh = [this] { m_Work.Refresh(); };

		m_BranchTable.OnBranchClicked = [this](const Core::Branch& branch) { SelectBranch(branch); };
		m_BranchTable.DrawBranchMenu = [this](const Core::Branch& branch) { RenderRefMenu(branch); };

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

	void GitView::Select(const GitWindowSelection& selection)
	{
		switch (selection.View)
		{
		case GitWindowView::Work:
			ShowWork(false);
			return;
		case GitWindowView::Thread:
			OpenThread(selection.Branch);
			return;
		case GitWindowView::History:
			SelectRef(selection.Branch.empty() ? std::nullopt : std::optional<std::string>{selection.Branch});
			return;
		case GitWindowView::Changes:
		case GitWindowView::Branches:
			m_PendingRestore.reset();
			m_Selection = {selection.View, {}};
			return;
		}
	}

	void GitView::OpenThread(const std::string& branchFullName)
	{
		if (branchFullName.empty())
		{
			ShowWork(false);
			return;
		}

		m_PendingRestore.reset();
		m_Selection = {GitWindowView::Thread, branchFullName};
	}

	void GitView::ShowWork(bool expandReady)
	{
		m_PendingRestore.reset();
		m_Selection = {GitWindowView::Work, {}};

		if (expandReady)
		{
			m_WorkView.ExpandReady();
		}
	}

	GitWindowState GitView::CaptureWindowState() const
	{
		// WARNING: A restore still waiting for the branches is what the window will show, not the Work it shows meanwhile.
		if (m_PendingRestore.has_value())
		{
			return *m_PendingRestore;
		}

		return GitWindowState{.View = m_Selection.View, .Branch = m_Selection.Branch};
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
		const bool namesBranch = (m_PendingRestore->View == GitWindowView::Thread ||
								  m_PendingRestore->View == GitWindowView::History) &&
								 !m_PendingRestore->Branch.empty();

		if (namesBranch && !branches)
		{
			return;
		}

		const GitWindowState state = *m_PendingRestore;
		m_PendingRestore.reset();

		Select(SelectionToRestore(state, BranchesOrNone(branches)));
	}

	void GitView::FollowWork(bool panelOnScreen)
	{
		ApplyPendingRestore();

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		// WARNING: Only the Git window reads the facts, and a read lists every worktree and counts the changes in
		// each, so a window that is not on screen asks for none; the snapshot it missed is asked for once it is.
		if (panelOnScreen && branches && branches != m_FollowedBranches)
		{
			m_FollowedBranches = branches;
			m_Work.FollowBranches(*branches);
		}

		// WARNING: The sessions are indexed only once something asks: the cards' titles come from them, and the
		// Sessions panel may never have been shown.
		if (panelOnScreen && WantTitles)
		{
			WantTitles();
		}

		RebuildBoardWhenOutOfDate();
		m_Work.SetPolled(m_CardWorktrees, panelOnScreen && m_Selection.View == GitWindowView::Work);
	}

	void GitView::RebuildBoardWhenOutOfDate()
	{
		BoardInputs inputs{.Facts = m_Work.GetFacts(),
						   .Branches = m_Model.GetBranches(),
						   .Status = m_Model.GetStatus(),
						   .Titles = TitlesNow ? TitlesNow() : Core::Ref<const SessionTitles>{}};

		if (m_BoardBuiltFrom == inputs)
		{
			return;
		}

		const Core::Ref<const SessionTitles>& titles = inputs.Titles;

		m_Board = BuildWorkBoard(WorkBoardInput{
			.Branches = inputs.Branches.get(),
			.Facts = inputs.Facts.get(),
			.WorkingCopyChanges = ChangeCountOf(inputs.Status),
			.WorkingDirectory = m_Model.GetWorkingDirectory(),
			.TitleOf =[&titles](const std::filesystem::path& worktree) -> std::optional<std::string> {
				return titles ? SessionTitleFor(*titles, worktree) : std::nullopt;
			}});

		m_CardWorktrees.clear();

		for (const WorkCard& card : m_Board.Cards)
		{
			if (card.Kind != WorkCardKind::WorkingCopy && card.Worktree.has_value())
			{
				m_CardWorktrees.push_back(*card.Worktree);
			}
		}

		m_BoardBuiltFrom = std::move(inputs);
		++m_BoardVersion;
	}

	void GitView::RebuildNavigatorWhenOutOfDate()
	{
		const bool showsStash = m_Selection.View == GitWindowView::History && m_SelectedStash.has_value();

		NavigatorInputs inputs{.BoardVersion = m_BoardVersion,
							   .Branches = m_Model.GetBranches(),
							   .ChangeCount = ChangeCountOf(m_Model.GetStatus()),
							   .Selection = showsStash ? GitWindowSelection{GitWindowView::History, kStashRef} : m_Selection,
							   .Now = std::chrono::floor<std::chrono::minutes>(NowInSeconds())};

		if (m_NavigatorBuiltFrom == inputs)
		{
			return;
		}

		m_NavigatorRows = BuildNavigatorRows(NavigatorInput{.Board = &m_Board,
															.ChangeCount = inputs.ChangeCount,
															.Branches = inputs.Branches.get(),
															.Selection = inputs.Selection,
															.Now = inputs.Now});
		m_NavigatorBuiltFrom = std::move(inputs);
	}

	void GitView::RenderNavigator()
	{
		RenderNavigatorHeader();
		RebuildNavigatorWhenOutOfDate();

		const ImVec2 fillTheRest{0.0f, 0.0f};

		if (ImGui::BeginChild("##navigator", fillTheRest))
		{
			const ListBodyScope listBody;
			ImGui::Indent(ListRowEndInset());

			const Core::Ref<const std::vector<Core::Branch>>& branches = m_NavigatorBuiltFrom->Branches;
			m_Navigator.Draw(m_NavigatorRows, BranchesOrNone(branches));

			const std::optional<Core::Error>& branchesError = m_Model.GetBranchesError();

			if (!branches && branchesError.has_value())
			{
				ImGui::TextDisabled("%s", FitToWidth(branchesError->Message, ImGui::GetContentRegionAvail().x).c_str());
			}
			else if (!branches)
			{
				ImGui::PushID("##branchesLoading");
				Core::LoadingLine("Loading branches");
				ImGui::PopID();
			}

			// WARNING: Drawn before the branch list is read, the stashes sit under the loader, then jump down once
			// the branches land.
			if (branches || branchesError.has_value())
			{
				RenderStashes();
			}

			ImGui::Unindent(ListRowEndInset());
		}

		ImGui::EndChild();
	}

	void GitView::RenderNavigatorHeader()
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

	void GitView::RenderMain(const GitPalette& palette)
	{
		switch (m_Selection.View)
		{
		case GitWindowView::Work:
			RenderWork();
			return;
		case GitWindowView::Thread:
			RenderThread();
			return;
		case GitWindowView::Changes:
			RenderChanges(palette);
			return;
		case GitWindowView::History:
			RenderHistory(palette);
			return;
		case GitWindowView::Branches:
			RenderBranchTable();
			return;
		}
	}

	void GitView::RenderWork()
	{
		// The branches are read before the facts are asked for, so the wait for them is the board's wait too.
		const bool branchesPending = !m_Model.GetBranches() && !m_Model.GetBranchesError().has_value();
		const ImVec2 fillTheRest{0.0f, 0.0f};

		if (ImGui::BeginChild("##work", fillTheRest))
		{
			m_WorkView.Draw(m_Board, m_Work.IsReading() || branchesPending, m_Work.GetError(), NowInSeconds());
		}

		ImGui::EndChild();
	}

	const WorkCard* GitView::CardOfBranch(const std::string& branchFullName) const
	{
		const auto found = std::ranges::find_if(m_Board.Cards, [&branchFullName](const WorkCard& card) {
			return card.Kind == WorkCardKind::Branch && card.Branch == branchFullName;
		});

		return found == m_Board.Cards.end() ? nullptr : &*found;
	}

	void GitView::RenderThread()
	{
		// A copy: the header's button and the review below may each change the selection.
		const std::string branch = m_Selection.Branch;
		const std::string trunkName = m_Board.Trunk.has_value() ? Core::ShortBranchName(*m_Board.Trunk) : kNoTrunkName;
		const std::optional<std::string> base = SessionBaseOf ? SessionBaseOf(branch) : std::nullopt;
		const bool againstTrunk = !base.has_value() || base == m_Board.Trunk;

		const ThreadHeaderInput header{
			.Card = CardOfBranch(branch),
			.TrunkName = trunkName,
			.AgainstBase = againstTrunk ? std::nullopt : std::optional<std::string>{Core::ShortBranchName(*base)},
			.Branch = branch,
			.Viewed = ReviewProgressOf ? ReviewProgressOf(branch) : std::nullopt,
			.Colours = ReadChipColours(m_Scheme.Get())};

		switch (DrawThreadHeader(header))
		{
		case ThreadHeaderClick::OpenInOwnTab:
			if (OpenReviewTab)
			{
				OpenReviewTab(branch);
			}
			break;
		case ThreadHeaderClick::BackToWork:
			ShowWork(false);
			return;
		case ThreadHeaderClick::None:
			break;
		}

		if (!DrawReview)
		{
			Core::EmptyState("The review cannot be shown in this window.");
			return;
		}

		DrawReview(branch);

		if (TakeReviewClosed && TakeReviewClosed())
		{
			ShowWork(false);
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
		EmphasisedLine(FitToWidth(std::string(ICON_LC_HISTORY " ") + HistoryTitle(), ImGui::GetContentRegionAvail().x));
	}

	void GitView::RenderBranchTable()
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		// WARNING: The held snapshots stay as they are on this path: the table builds no rows without branches, so
		// it still remembers the addresses of the ones it was handed last.
		if (!branches)
		{
			if (const std::optional<Core::Error>& error = m_Model.GetBranchesError(); error.has_value())
			{
				Core::EmptyState("Could not read the branches: " + error->Message);
			}
			else
			{
				Core::LoadingState("Loading branches");
			}

			return;
		}

		m_TableBranches = branches;
		m_TableAuthors = m_Model.GetBranchAuthors();
		m_TableFacts = m_Work.GetFacts();

		m_BranchTable.Draw(BranchTableInput{.Branches = m_TableBranches.get(),
											.Authors = m_TableAuthors.get(),
											.Facts = m_TableFacts.get(),
											.HeldByWorktree = BranchesHeldByLinkedWorktrees(m_TableFacts.get()),
											.Now = NowInSeconds()},
						   ReaderTimeZone());
	}
}

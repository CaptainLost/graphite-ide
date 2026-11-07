#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Ascii.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"

#include "Git/Branches/BranchLookup.h"
#include "Git/Compare/HeaderShares.h"
#include "Git/Compare/HistoryBanner.h"
#include "Git/GitStatusText.h"
#include "Git/History/CommitLabels.h"
#include "Git/Tags/TagLookup.h"
#include "Git/Window/MainLineQuestion.h"
#include "Git/Window/GitWindowRestore.h"
#include "Git/Window/WindowBar.h"
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

		constexpr float kKeepFontSize = 0.0f;
		constexpr float kVsPickerGap = 4.0f; // mockup px between the vs button and its popup

		const std::vector<Core::Branch>& BranchesOrNone(const Core::Ref<const std::vector<Core::Branch>>& branches)
		{
			static const std::vector<Core::Branch> kNoBranches;
			return branches ? *branches : kNoBranches;
		}

		const std::vector<Core::Tag>& TagsOrNone(const Core::Ref<const std::vector<Core::Tag>>& tags)
		{
			static const std::vector<Core::Tag> kNoTags;
			return tags ? *tags : kNoTags;
		}

		std::chrono::sys_seconds NowInSeconds()
		{
			return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
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

			for (std::size_t ref = 0; ref < branches.size(); ++ref)
			{
				if (branches[ref].IsHead)
				{
					return ref;
				}
			}

			return std::nullopt;
		}

		// The stash's place in the list; empty when the list no longer holds it.
		std::optional<std::size_t> IndexOfStash(const std::vector<Core::StashEntry>& stashes, const Core::Oid& id)
		{
			for (std::size_t index = 0; index < stashes.size(); ++index)
			{
				if (stashes[index].Id == id)
				{
					return index;
				}
			}

			return std::nullopt;
		}

		// The failure a read left, or null when it has none, as the window bar takes it.
		const Core::Error* FailureOf(const std::optional<Core::Error>& error)
		{
			return error.has_value() ? &*error : nullptr;
		}

		// The icon the history's name follows, for a history that names a ref or a stash.
		const char* HistoryIcon(bool stash, bool tag)
		{
			if (stash)
			{
				return ICON_LC_ARCHIVE;
			}

			return tag ? ICON_LC_TAG : ICON_LC_GIT_BRANCH;
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
		const Core::Ref<const std::vector<Core::Tag>> tags = m_Model.GetTags();
		const RefListing branchListing{.Listed = branches != nullptr,
									   .Failed = !branches && m_Model.GetBranchesError().has_value()};
		const RefListing tagListing{.Listed = tags != nullptr, .Failed = !tags && m_Model.GetTagsError().has_value()};

		if (RestoreWaitsForListing(*m_PendingRestore, branchListing, tagListing))
		{
			return;
		}

		const GitWindowState state = *m_PendingRestore;
		m_PendingRestore.reset();

		const std::optional<std::string> ref = RefToRestore(state, BranchesOrNone(branches), TagsOrNone(tags));

		// HEAD's history, already what the window shows, is asked for once the history is first drawn.
		if (ref == m_SelectedRef && !m_SelectedStash.has_value())
		{
			return;
		}

		SelectRef(ref);
	}

	void GitView::RenderWindowBar()
	{
		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const Core::Ref<const std::vector<Core::Remote>> remotes = m_Model.GetRemotes();
		const std::optional<std::string> checkingOut = CurrentWait().CheckingOut;
		const std::filesystem::path& folder = m_Model.GetWorkingDirectory();

		// WARNING: Rebuilt only when what it describes changed: the checked-out branch is looked up in every branch.
		const bool remotesFailed = m_Model.GetRemotesError().has_value();
		const bool headFailed = !head && m_Model.GetHeadError().has_value();
		const WindowBarSource source{head, branches, remotes, remotesFailed, headFailed, checkingOut, folder};

		if (m_WindowBarSource != source)
		{
			const std::optional<Core::Error>& remotesError = m_Model.GetRemotesError();
			const std::string* remotesFailure = remotesFailed && !remotes ? &remotesError->Message : nullptr;

			const SettledWindowBar settled = SettleWindowBar(
				WindowBarInput{folder, head.get(), branches.get(), remotes.get(), false, remotesFailure}, headFailed,
				m_WindowBarSettled.has_value() ? &*m_WindowBarSettled : nullptr);

			m_WindowBarText = settled.Text;
			m_WindowBarSettled = settled.Complete ? std::optional<WindowBarText>{settled.Text} : std::nullopt;

			if (settled.Complete && checkingOut.has_value())
			{
				m_WindowBarText.Busy = true;
				m_WindowBarText.Head = CheckingOutText(*checkingOut);
			}

			m_WindowBarSource = source;
		}

		const std::optional<Core::Error>& headError = m_Model.GetHeadError();
		const bool showsHeadFailure = headFailed && m_WindowBarSettled.has_value();
		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());

		// WARNING: A failed read is said in the bar, whose height is fixed: drawn as a line after the window's table it
		// stood below the window's room and made the whole window scroll. The lines' failure is said whether or not a read
		// is held: a later read or a change that fails leaves the rows of the last good read on screen. A commit's diff
		// that fails is said in its details, so it neither comes nor goes here with the selection.
		const std::string problem = WindowBarProblem(WindowBarFailures{.Status = FailureOf(m_Model.GetStatusError()),
																	   .Branches = FailureOf(m_Model.GetBranchesError()),
																	   .Log = FailureOf(m_Model.GetLogError()),
																	   .Lines = FailureOf(m_Lines.GetError())});

		if (DrawWindowBar(WindowBarDrawing{.Text = m_WindowBarText,
										   .WorkingDirectory = folder,
										   .HeadFailure = showsHeadFailure ? std::string_view{headError->Message} : std::string_view{},
										   .Colours = colours,
										   .Bleed = ImGui::GetStyle().WindowPadding,
										   .Problem = problem}))
		{
			AnswerHeadChip();
		}

		const Core::Ref<const LineRead> read = m_Lines.GetRead();
		const std::optional<MainLineAsk> ask = MainLineToAsk(read.get(), m_MainLineDismissed);

		if (!ask.has_value())
		{
			return;
		}

		switch (DrawMainLineQuestion(*ask, colours))
		{
		case MainLineAnswer::UseGuess:
			m_Lines.Change(LineAnswerChange{.What = LineAnswerChange::Kind::UseAsMainLine, .Ref = ask->GuessRef});
			break;

		case MainLineAnswer::NotNow:
			m_MainLineDismissed = true;
			break;

		case MainLineAnswer::None:
			break;
		}
	}

	void GitView::RenderSidebar()
	{
		const int frame = ImGui::GetFrameCount();

		if (frame != m_SidebarDrawnOnFrame + 1)
		{
			++m_SidebarOpenings;
		}

		m_SidebarDrawnOnFrame = frame;

		RebuildSidebarWhenOutOfDate();

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const std::optional<SidebarWaitingRow> waiting = DescribeSidebarWait(SidebarWaitInput{
			.HasRead = m_SidebarBuiltFrom->Read != nullptr,
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
		const std::optional<Core::Error>& stashesError = m_Model.GetStashesError();
		const std::optional<Core::Error>& tagsError = m_Model.GetTagsError();

		SidebarInputs inputs{.Read = m_Lines.GetRead(),
							 .Stashes = m_Model.GetStashes(),
							 .StashesError = stashesError.has_value() ? std::optional<std::string>{stashesError->Message}
																	  : std::nullopt,
							 .Tags = m_Model.GetTags(),
							 .TagsError = tagsError.has_value() ? std::optional<std::string>{tagsError->Message} : std::nullopt,
							 .Authors = m_SearchedAuthors.Follow(m_Model.GetBranchAuthors(), m_Sidebar.GetSearch()),
							 .Search = m_Sidebar.GetSearch(),
							 .ChoicesVersion = m_SidebarChoicesVersion,
							 .MergedChoicesVersion = m_SidebarMergedChoicesVersion,
							 .Openings = m_SidebarOpenings,
							 .ShownRef = SidebarShownRef()};

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

		const Core::Error* const failedStashRead = stashesError.has_value() ? &*stashesError : nullptr;
		const Core::Error* const failedTagRead = tagsError.has_value() ? &*tagsError : nullptr;

		if (inputs.Read)
		{
			// What content checks found so far, read only at the moments the snapshot is taken: a verdict that lands later
			// fills its row's mark in place and hides no row until the next such moment.
			const LineRead& read = *inputs.Read;
			const std::function<bool(std::size_t)> mergedNow = [this, &read](std::size_t branch) {
				return IsMergedByContent(m_Content.KnownMarkOf(read.Model.Branches[branch]));
			};
			m_HiddenByContent.Follow(inputs, mergedNow);

			const std::function<bool(std::size_t)> mergedByContent = [this](std::size_t branch) {
				return m_HiddenByContent.IsMerged(branch);
			};

			m_SidebarRows = BuildSidebarRows(SidebarInputFor(inputs, mergedByContent));
		}
		else
		{
			m_SidebarRows = BuildStashRows(inputs.Stashes.get(), failedStashRead, inputs.Tags.get(), failedTagRead,
											  inputs.Search, m_OpenByUser);
		}

		m_SidebarBuiltFrom = std::move(inputs);
	}

	SidebarInput GitView::SidebarInputFor(const SidebarInputs& inputs,
										  const std::function<bool(std::size_t)>& mergedByContent) const
	{
		const std::optional<Core::Error>& stashesError = m_Model.GetStashesError();
		const std::optional<Core::Error>& tagsError = m_Model.GetTagsError();

		return SidebarInput{.Read = *inputs.Read,
							.Stashes = inputs.Stashes.get(),
							.Authors = inputs.Authors.get(),
							.Search = inputs.Search,
							.OpenByUser = m_OpenByUser,
							.ShowingMerged = m_ShowingMerged,
							.StashesError = stashesError.has_value() ? &*stashesError : nullptr,
							.ShownRef = inputs.ShownRef,
							.MergedByContent = &mergedByContent,
							.Tags = inputs.Tags.get(),
							.TagsError = tagsError.has_value() ? &*tagsError : nullptr};
	}

	void GitView::AnswerHeadChip()
	{
		// WARNING: HEAD's history first, so the rows are built with HEAD's branch as the shown one: its row is selected in
		// the same frame the sidebar scrolls to it, and the reveal reads the groups that showing it opens.
		SelectRef(std::nullopt);

		// The rows as the sidebar will draw them next, so the reveal reads what the reader is about to see.
		RebuildSidebarWhenOutOfDate();

		if (!m_SidebarBuiltFrom.has_value() || !m_SidebarBuiltFrom->Read)
		{
			return;
		}

		const std::function<bool(std::size_t)> mergedByContent = [this](std::size_t branch) {
			return m_HiddenByContent.IsMerged(branch);
		};

		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();
		const std::optional<HeadRowReveal> reveal =
			RevealHeadRow(head.get(), SidebarInputFor(*m_SidebarBuiltFrom, mergedByContent));

		if (!reveal.has_value())
		{
			return;
		}

		if (reveal->ClearsSearch)
		{
			m_Sidebar.ClearSearch();
		}

		for (const std::string& key : reveal->Opens)
		{
			m_OpenByUser[key] = true;
		}

		if (!reveal->Opens.empty())
		{
			++m_SidebarChoicesVersion;
		}

		// The rows are built again before the sidebar is drawn, from the search and the open sections changed here.
		m_Sidebar.ScrollToRef(reveal->Ref);
	}

	std::string GitView::SidebarShownRef() const
	{
		if (m_SelectedStash.has_value() || RestoreWaits())
		{
			return {};
		}

		if (m_SelectedRef.has_value())
		{
			return *m_SelectedRef;
		}

		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();

		return head && head->Kind == Core::HeadKind::Branch ? Core::LocalBranchReference(head->BranchName) : std::string{};
	}

	const Core::Branch* GitView::SidebarBranchAt(std::optional<std::size_t> ref) const
	{
		const Core::Ref<const LineRead>& read = m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Read : nullptr;

		if (!read || !read->Branches || !ref.has_value() || *ref >= read->Branches->size())
		{
			return nullptr;
		}

		return &(*read->Branches)[*ref];
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

	const Core::Tag* GitView::SidebarTagOf(const SidebarRow& row) const
	{
		const Core::Ref<const std::vector<Core::Tag>>& tags = m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Tags : nullptr;

		if (!tags || !row.Tag.has_value() || *row.Tag >= tags->size())
		{
			return nullptr;
		}

		return &(*tags)[*row.Tag];
	}

	SidebarRowText GitView::SidebarTextOf(const SidebarRow& row) const
	{
		if (row.Kind == SidebarRowKind::Tag)
		{
			const Core::Tag* tag = SidebarTagOf(row);
			return tag != nullptr ? DescribeTagRow(*tag, NowInSeconds(), ReaderTimeZone()) : SidebarRowText{};
		}

		if (row.Kind == SidebarRowKind::Stash)
		{
			const Core::StashEntry* stash = SidebarStashOf(row);
			return stash != nullptr ? DescribeStashRow(*stash, NowInSeconds(), ReaderTimeZone()) : SidebarRowText{};
		}

		if (!m_SidebarLines.has_value())
		{
			return SidebarRowText{};
		}

		SidebarTextInput input{.Read = *m_SidebarBuiltFrom->Read,
							   .Lines = *m_SidebarLines,
							   .UserEmail = m_Model.GetUserEmail(),
							   .MostCommonLine = m_MostCommonLine,
							   .Now = NowInSeconds()};
		input.WorktreeOf = [this](std::string_view fullName) { return m_Worktrees.OtherWorktreeOf(fullName); };
		input.IsDirty = [this](const std::filesystem::path& worktree) { return m_Worktrees.DirtyInView(worktree); };
		input.CountFailureOf = [this](const std::filesystem::path& worktree) { return m_Worktrees.CountFailureOf(worktree); };
		input.ContentOf = [this](const Core::BranchLine& line) { return m_Content.MarkOf(line); };

		return DescribeSidebarRow(row, input);
	}

	std::optional<std::size_t> GitView::SelectedTagIndex() const
	{
		const Core::Ref<const std::vector<Core::Tag>>& tags = m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Tags : nullptr;

		if (!tags || !ShowsTag())
		{
			return std::nullopt;
		}

		return IndexOfTag(*tags, *m_SelectedRef);
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
				selection.Stash = IndexOfStash(*stashes, *m_SelectedStash);
			}

			return selection;
		}

		selection.Tag = SelectedTagIndex();

		if (!m_SidebarLines.has_value())
		{
			return selection;
		}

		if (m_SelectedRef.has_value())
		{
			selection.Ref = m_SidebarLines->IndexOf(*m_SelectedRef);
			return selection;
		}

		// HEAD's row is the branch HEAD names, which a read made before a checkout does not mark as checked out yet.
		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();

		if (!head)
		{
			selection.Ref = m_HeadRef;
		}
		else if (head->Kind == Core::HeadKind::Branch)
		{
			selection.Ref = m_SidebarLines->IndexOf(Core::LocalBranchReference(head->BranchName));
		}

		return selection;
	}

	void GitView::AnswerSidebarClick(const SidebarRow& row)
	{
		switch (row.Kind)
		{
		case SidebarRowKind::Branch:
		case SidebarRowKind::Base:
		{
			const std::optional<std::size_t> opened =
				m_SidebarLines.has_value() ? RefOpenedBy(row, *m_SidebarBuiltFrom->Read, *m_SidebarLines) : std::nullopt;

			if (const Core::Branch* branch = SidebarBranchAt(opened))
			{
				// WARNING: The rows can be a read behind the branches, so a row clicked may name a branch already gone;
				// its history is then HEAD's, as for a shown branch that goes.
				const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
				const bool isStillThere = BranchWithFullName(BranchesOrNone(branches), branch->FullName) != nullptr;

				SelectRef(isStillThere ? std::optional<std::string>{branch->FullName} : std::nullopt);
			}
			return;
		}
		case SidebarRowKind::Stash:
			if (const Core::StashEntry* stash = SidebarStashOf(row))
			{
				SelectStash(stash->Id);
			}
			return;
		case SidebarRowKind::Tag:
			if (const Core::Tag* tag = SidebarTagOf(row))
			{
				// WARNING: The rows can be a read behind the tags, so a row clicked may name a tag already gone; its history is
				// then HEAD's, as for a branch.
				const Core::Ref<const std::vector<Core::Tag>> listed = m_Model.GetTags();
				const bool isStillThere = listed && TagWithFullName(*listed, tag->FullName) != nullptr;

				SelectRef(isStillThere ? std::optional<std::string>{tag->FullName} : std::nullopt);
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
		case SidebarRowKind::Notice:
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
			++m_SidebarMergedChoicesVersion;
			return;
		case SidebarLink::HideMerged:
			m_ShowingMerged.erase(remote);
			++m_SidebarMergedChoicesVersion;
			return;
		case SidebarLink::WhyBases:
			OpenVsPicker(ImGui::GetMousePos());
			return;
		}
	}

	void GitView::DrawSidebarRowMenu(const SidebarRow& row)
	{
		const bool isStash = row.Kind == SidebarRowKind::Stash;
		const RowMenuRefs refs =
			m_SidebarLines.has_value() ? RowMenuRefsOf(row, *m_SidebarBuiltFrom->Read, *m_SidebarLines) : RowMenuRefs{};
		const Core::Branch* branch = SidebarBranchAt(refs.Menu);
		const Core::StashEntry* stash = isStash ? SidebarStashOf(row) : nullptr;

		// A base row's lines items are the base's, whichever branch its menu is for; Compare with… opens what its click does.
		const Core::Branch* const linesBranch = SidebarBranchAt(refs.Lines);
		const Core::Branch* const comparedBranch = SidebarBranchAt(refs.Compared);

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

		if (branch != nullptr && linesBranch != nullptr && comparedBranch != nullptr)
		{
			RenderRefMenuItems(*branch, linesBranch->FullName, comparedBranch->FullName);
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
		// WARNING: No name while the history to show is still being decided, or while HEAD is read: "HEAD" and then the
		// branch's name would be two names in a row for one history.
		if (RestoreWaits())
		{
			return {};
		}

		if (m_SelectedStash.has_value())
		{
			const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Model.GetStashes();

			const std::optional<std::size_t> found = stashes ? IndexOfStash(*stashes, *m_SelectedStash) : std::nullopt;

			if (found.has_value() && !(*stashes)[*found].Message.empty())
			{
				return "Stash \xC2\xB7 " + (*stashes)[*found].Message;
			}

			return "Stash";
		}

		if (ShowsTag())
		{
			const Core::Ref<const std::vector<Core::Tag>> tags = m_Model.GetTags();
			const Core::Tag* tag = TagWithFullName(TagsOrNone(tags), *m_SelectedRef);

			return tag != nullptr ? tag->Name : m_SelectedRef->substr(Core::kTagPrefix.size());
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
			return m_Model.GetHeadError().has_value() ? "HEAD" : std::string{};
		}

		switch (head->Kind)
		{
		case Core::HeadKind::Branch:
		case Core::HeadKind::Unborn:
			return head->BranchName;
		case Core::HeadKind::Detached:
			return "detached at " + (head->Commit.has_value() ? head->Commit->ShortHex() : std::string{});
		}

		return "HEAD";
	}

	void GitView::RenderHistoryHeader()
	{
		const float rowWidth = ImGui::GetContentRegionAvail().x;
		const float rowStartX = ImGui::GetCursorScreenPos().x;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());

		// What the shown branch is compared with, from the read the sidebar's lines view.
		const std::string shownRef = ShownRefName();
		const LineRead* const read = m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Read.get() : nullptr;
		const BranchLines* const lines = CurrentLines();

		VsLabel label;
		std::optional<std::string> banner;

		// The label and the banner come in the update that shows the rows under them, which waits for the distance they
		// count, so the three arrive together and the label at its final width.
		if (read != nullptr && lines != nullptr && !shownRef.empty() && HistoryRowsNow() == HistoryRows::Shown)
		{
			const std::optional<ContentMark> content = ShownContentMark();
			const VsInput input{*read, *lines, shownRef, ShownDistance(*read, shownRef),
								content.has_value() ? content->Verdict : std::nullopt};
			const HeaderWords fresh{.Label = DescribeVs(input), .Banner = HistoryBannerFor(input)};
			const bool waits = HeaderWordsWait(MeasuresShownDistance(*read, shownRef), RowsOfLineShown(), ShownContentWaits());
			const HeaderWords& words = HeaderWordsToDraw(fresh, waits, shownRef, m_DrawnHeaderWords);

			label = words.Label;
			banner = words.Banner;

			if (!waits)
			{
				m_DrawnHeaderWords = DrawnHeaderWords{.ShownRef = shownRef, .Words = fresh};
			}
		}

		// An operation in progress belongs to HEAD, so it is said by HEAD's history and by the checked-out branch's.
		std::string operation;
		const bool isHeadHistory = !RestoreWaits() && !m_SelectedStash.has_value() &&
								   (!m_SelectedRef.has_value() || (m_HeadRef.has_value() && read != nullptr &&
																   read->Refs[*m_HeadRef].FullName == *m_SelectedRef));
		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();

		if (isHeadHistory && head)
		{
			operation = HeadOperationText(head->Operation);
		}

		// The remote of a remote branch stands dim before its name.
		std::string prefix;
		std::string title = HistoryTitle();

		if (m_SelectedRef.has_value() && read != nullptr && lines != nullptr)
		{
			const std::optional<std::size_t> index = lines->IndexOf(*m_SelectedRef);

			if (index.has_value() && !read->Refs[*index].Remote.empty())
			{
				prefix = read->Refs[*index].Remote + "/";
				title = read->Refs[*index].ShortName;
			}
		}

		// A branch's history is named after the branch icon, a stash's after the archive's, a tag's after the tag's, HEAD's
		// after its dot.
		ImGui::AlignTextToFramePadding();
		ImGui::BeginGroup();

		const bool namesHead = !m_SelectedStash.has_value() && !m_SelectedRef.has_value();

		if (RestoreWaits())
		{
			// Neither HEAD's dot nor a branch's icon before it is known which history this is.
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
		}
		else if (namesHead)
		{
			// As tall as the frame, so the dot's middle is the text's.
			const float dot = WindowBarEm(7.0f);
			ImGui::Dummy(ImVec2(dot, ImGui::GetFrameHeight()));
			const ImVec2 centre((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
								(ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
			ImGui::GetWindowDrawList()->AddCircleFilled(centre, dot * 0.5f, colours.Accent);
		}
		else
		{
			ImGui::TextUnformatted(HistoryIcon(m_SelectedStash.has_value(), ShowsTag()));
		}

		ImGui::SameLine();

		const float operationWidth =
			operation.empty() ? 0.0f : ImGui::CalcTextSize(operation.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
		const float prefixWidth = ImGui::CalcTextSize(prefix.c_str()).x;

		ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
		const float titleWidth = ImGui::CalcTextSize(title.c_str()).x;
		ImGui::PopFont();

		const HeaderShares shares = ShareHeader(HeaderWidths{.Row = rowWidth,
															 .Taken = ImGui::GetCursorScreenPos().x - rowStartX,
															 .VsButton = label.Lead.empty() ? 0.0f : VsButtonWidth(label),
															 .Switch = HistorySwitchWidth(),
															 .Operation = operationWidth,
															 .Prefix = prefixWidth,
															 .Title = titleWidth,
															 .Fields = FilterFieldsBasis(),
															 .Spacing = spacing});
		const float vsWidth = shares.VsButton;
		const float nameRoom = shares.NameRoom;

		if (!prefix.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colours.Dim);
			ImGui::TextUnformatted(FitToWidth(prefix, nameRoom).c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(0.0f, 0.0f);
		}

		ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
		EmphasisedLine(FitToWidth(title, std::max(0.0f, nameRoom - prefixWidth)));
		ImGui::PopFont();

		if (!operation.empty())
		{
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, colours.AskQuestion);
			ImGui::TextUnformatted(operation.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::EndGroup();

		// The branch the header names has its menu here too: the selected ref's, or HEAD's for HEAD's history.
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const std::vector<Core::Branch>& listed = BranchesOrNone(branches);
		const Core::Branch* const named =
			m_SelectedStash.has_value() ? nullptr
										: (m_SelectedRef.has_value() ? BranchWithFullName(listed, *m_SelectedRef) : CheckedOutBranch(listed));

		// Keyed by the branch, so a menu left open while HEAD moves elsewhere closes rather than naming the new branch.
		if (named != nullptr)
		{
			ImGui::PushID(named->FullName.c_str());
			RenderRefMenu(*named);
			ImGui::PopID();
		}

		if (!label.Lead.empty())
		{
			ImGui::SameLine();

			if (DrawVsButton(label, vsWidth))
			{
				OpenVsPicker(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + WindowBarEm(kVsPickerGap)));
			}
		}

		const bool switchApplies = !m_SelectedStash.has_value() && Core::Trimmed(m_Filter.Get().Path).empty();

		ImGui::SameLine();
		RenderFilterFields(ImGui::GetContentRegionAvail().x - HistorySwitchWidth() - spacing);

		ImGui::SameLine();

		if (DrawHistorySwitch(switchApplies))
		{
			RequestLog(HistoryReach{});
		}

		// Drawn by the list, under the Local changes row, which is on screen before the banner can come.
		m_HistoryBanner = banner;

		DrawVsPicker();
	}

	void GitView::OpenVsPicker(ImVec2 below)
	{
		const std::string shown = ShownRefName();

		if (!shown.empty())
		{
			m_VsPicker.Open(shown, below);
		}
	}

	void GitView::DrawVsPicker()
	{
		if (!m_VsPicker.IsOpen())
		{
			return;
		}

		const LineRead* const read = m_SidebarBuiltFrom.has_value() ? m_SidebarBuiltFrom->Read.get() : nullptr;
		const BranchLines* const lines = CurrentLines();

		// Without a read there is nothing to choose from.
		if (read == nullptr || lines == nullptr)
		{
			m_VsPicker.Close();
			return;
		}

		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());
		const ImGuiViewport* const viewport = ImGui::GetMainViewport();
		const std::function<std::optional<Core::LineDistance>(const Core::Oid&, const Core::Oid&)> distance =
			[this](const Core::Oid& from, const Core::Oid& to) { return m_Lines.DistanceOf(from, to); };

		const std::optional<LineAnswerChange> change =
			m_VsPicker.Draw(VsPickerView::Drawing{.Read = *read,
												  .Lines = *lines,
												  .Now = NowInSeconds(),
												  .DistanceBetween = distance,
												  .Colours = colours,
												  .RegionMin = viewport->WorkPos,
												  .RegionMax = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y)});

		if (change.has_value())
		{
			m_Lines.Change(*change);
		}
	}
}

#include "Review/ReviewWindows.h"

#include "Git/History/CommitLabels.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "UI/FileIcons.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "UI/SplitResults.h"
#include "UI/TickBox.h"
#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/TextMetrics.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr float kFileColumnEms = 24.0f;
		constexpr float kNoSpacing = 0.0f;
		constexpr int kReviewColumnCount = 2;
		constexpr std::size_t kCommitsInTooltip = 20;
		constexpr float kCommitTooltipWrapEms = 35.0f;

		constexpr int kButtonsWithMarks = 2;
		constexpr int kButtonsWithoutMarks = 1;

		constexpr const char* kOnlyWhitespace = "Only whitespace changed.";
		constexpr const char* kDiffUnreadable = "The diff could not be read.";
		constexpr const char* kSinceViewedUnreadable = "The diff since you viewed could not be read.";
		constexpr const char* kUpdatingChanges = "Updating the changes";

		constexpr bool kNotUpdating = false;

		void DrawDiffNotLoaded(bool failed)
		{
			if (failed)
			{
				Core::EmptyState(kDiffUnreadable);
				return;
			}

			Core::LoadingState("Loading the diff");
		}

		enum class SinceViewedDiff
		{
			Available,
			ViewedVersionGone,
			Unreadable
		};

		SessionSettings ReadSettings(const ReviewSessions& sessions, const std::string& branch)
		{
			const ReviewSession* session = sessions.Find(branch);
			return session != nullptr ? session->Settings : SessionSettings{};
		}

		std::string UncommittedProblem(const Core::Error& error, const std::filesystem::path& worktree)
		{
			if (error.Code == Core::ErrorCode::NotFound)
			{
				return std::format("The worktree at {} is gone.", Core::ToUtf8(worktree));
			}

			return std::format("Uncommitted changes could not be read: {}", error.Message);
		}

		std::string CountsText(const Core::LineCounts& counts)
		{
			return std::format("+{} -{}", counts.Added, counts.Removed);
		}

		std::string ViewedOfTotalText(const ReviewTreeRow& row)
		{
			return std::format("{}/{}", row.Viewed, row.Total);
		}

		// WARNING: No padding at the list's sides, or its rows' highlights would stop half of it short of the edges;
		// the rows keep a list row's inset from the edges themselves.
		void PushListPadding()
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		}

		void DrawAtRight(const std::string& text, float rowRight)
		{
			ImGui::SameLine();
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - Core::TextWidth(text)));
			ImGui::TextDisabled("%s", text.c_str());
		}

		std::string HeadingNote(const ReviewTreeRow& row, const std::string& filter)
		{
			const bool noneLeftOnScreen = row.Total > 0 && row.Shown == 0;

			if (!noneLeftOnScreen)
			{
				return std::string{};
			}

			return filter.empty() ? std::string{"All viewed."} : std::string{"No files match."};
		}

		bool DrawHeadingRow(const ReviewTreeRow& row, const std::string& note, const Core::LineCounts& groupCounts)
		{
			const ImVec2 rowStart = ImGui::GetCursorPos();
			const float rowRight = ListRowEndPosX();
			const bool notSelected = false;
			const bool clicked = ListRowSelectable("##heading", notSelected, ImGuiSelectableFlags_AllowOverlap);

			ImGui::SetCursorPos(ImVec2(rowStart.x, ListRowTextTop(rowStart.y)));
			DrawChevron(!row.Folded);
			ImGui::SameLine();

			if (row.Loading)
			{
				ImGui::TextUnformatted(FitToWidth(GroupName(row.Group), rowRight - ImGui::GetCursorPosX()).c_str());
				return clicked;
			}

			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const std::string viewedText = ViewedOfTotalText(row);
			const bool showsLineCounts = row.Group == ReviewGroup::Uncommitted;
			const std::string countsText = showsLineCounts ? CountsText(groupCounts) : std::string{};
			const float trailingWidth =
				Core::TextWidth(viewedText) + (showsLineCounts ? Core::TextWidth(countsText) + spacing : 0.0f);
			const float textRoom = rowRight - trailingWidth - spacing - ImGui::GetCursorPosX();

			const std::string heading = FitToWidth(GroupHeadingText(row.Group, row.Shown, row.Total), textRoom);
			ImGui::TextUnformatted(heading.c_str());

			const float noteRoom = textRoom - Core::TextWidth(heading) - spacing;
			const bool noteFitsWhole = !note.empty() && FitToWidth(note, noteRoom) == note;

			if (noteFitsWhole)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", note.c_str());
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - trailingWidth));

			if (showsLineCounts)
			{
				ImGui::TextDisabled("%s", countsText.c_str());
				ImGui::SameLine();
			}

			ImGui::TextDisabled("%s", viewedText.c_str());

			return clicked;
		}

		void DrawLoadingRow(const ReviewTreeRow& row)
		{
			const float indent = static_cast<float>(row.Depth) * ImGui::GetStyle().IndentSpacing;
			const ImVec2 rowStart = ImGui::GetCursorScreenPos();

			ImGui::SetCursorScreenPos(ImVec2(rowStart.x + indent, ListRowTextTop(rowStart.y)));
			Core::LoadingLine(row.Label.c_str());

			// WARNING: The clipper counts every row as one list row pitch, so the loader stands on a list row's text line
			// and the row is laid out that tall, or the rows below it would stand short of where the clipper seeks them.
			ImGui::SetCursorScreenPos(rowStart);
			ImGui::ItemSize(ImVec2(0.0f, ListRowLineHeight()));
		}

		// WARNING: The header's text sits a frame padding down to line up with its tick box, and the loader draws its text
		// where its row starts. In a group it takes that offset without moving the rest of the line or what is under it.
		void DrawHeaderLoader(const char* text)
		{
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
			Core::LoadingLine(text);
			ImGui::EndGroup();
		}

		std::string FolderCountText(const ReviewTreeRow& row, RowMarks marks)
		{
			switch (marks)
			{
			case RowMarks::Shown:
				return ViewedOfTotalText(row);
			case RowMarks::Hidden:
				return std::string{};
			}

			return std::string{};
		}

		bool DrawFolderRow(const ReviewTreeRow& row, float indent, RowMarks marks)
		{
			const ImVec2 rowStart = ImGui::GetCursorPos();
			const float rowRight = ListRowEndPosX();
			const bool notSelected = false;
			const bool clicked = ListRowSelectable("##folder", notSelected, ImGuiSelectableFlags_AllowOverlap);

			ImGui::SetCursorPos(ImVec2(rowStart.x + indent, ListRowTextTop(rowStart.y)));
			DrawChevron(!row.Folded);
			ImGui::SameLine();
			DrawFolderIcon(!row.Folded, ImGui::GetCursorScreenPos());
			ImGui::Dummy(ImVec2(IconToNameOffset(), ImGui::GetTextLineHeight()));
			ImGui::SameLine(kNoSpacing, kNoSpacing);

			const std::string countText = FolderCountText(row, marks);
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float countWidth = countText.empty() ? 0.0f : Core::TextWidth(countText) + spacing;
			const float labelRoom = rowRight - countWidth - ImGui::GetCursorPosX();
			ImGui::TextUnformatted(FitToWidth(row.Label, labelRoom).c_str());

			if (!countText.empty())
			{
				DrawAtRight(countText, rowRight);
			}

			return clicked;
		}

		void ToggleFolder(std::set<MarkKey>& folded, const ReviewTreeRow& row)
		{
			const MarkKey folder{.Group = row.Group, .Path = row.FolderPath};

			if (folded.contains(folder))
			{
				folded.erase(folder);
				return;
			}

			folded.insert(folder);
		}

		enum class NameTone
		{
			Bright,
			Dimmed
		};

		void DrawPathLabel(const PathLabel& label, float room, NameTone tone)
		{
			const std::string prefix = label.Origin + label.Directory;
			const float prefixRoom = room - Core::TextWidth(label.Name);
			const bool prefixHasRoom = !prefix.empty() && prefixRoom > Core::TextWidth(kEllipsis);

			float nameRoom = room;

			if (prefixHasRoom)
			{
				const std::string shownPrefix = FitToWidth(prefix, prefixRoom);
				const float noOffset = 0.0f;
				const float noSpacing = 0.0f;

				ImGui::TextDisabled("%s", shownPrefix.c_str());
				ImGui::SameLine(noOffset, noSpacing);
				nameRoom -= Core::TextWidth(shownPrefix);
			}

			const std::string shownName = FitToWidth(label.Name, nameRoom);

			switch (tone)
			{
			case NameTone::Bright:
				ImGui::TextUnformatted(shownName.c_str());
				break;
			case NameTone::Dimmed:
				ImGui::TextDisabled("%s", shownName.c_str());
				break;
			}
		}

		bool DrawViewedBox(bool* viewed, ViewedState state, const GitPalette& palette)
		{
			switch (state)
			{
			case ViewedState::ChangedSinceViewed:
				return DrawTickBox("##viewed", viewed, ImGui::ColorConvertU32ToFloat4(palette.GutterModified));
			case ViewedState::NotViewed:
			case ViewedState::Viewed:
				return DrawTickBox("##viewed", viewed);
			}

			return DrawTickBox("##viewed", viewed);
		}

		struct FileRowLook
		{
			bool IsSelected = false;
			RowMarks Marks = RowMarks::Shown;
			ViewedState Viewed = ViewedState::NotViewed;
			bool CanOpen = false;
			float Indent = 0.0f;
			Core::LineCounts Counts;
		};

		struct FileRowActions
		{
			bool Selected = false;
			bool ToggledViewed = false;
			bool OpenInEditor = false;
			bool CopyPath = false;
		};

		PathLabel LabelInShape(const Core::FileDiff& file, ListShape shape)
		{
			const PathLabel wholePath = LabelOf(file);

			switch (shape)
			{
			case ListShape::Tree:
				return PathLabel{.Name = wholePath.Name};
			case ListShape::Flat:
				return wholePath;
			}

			return wholePath;
		}

		std::string ChangeTooltip(const Core::FileDiff& file, const PathLabel& label)
		{
			const bool originIsHidden = label.Origin.empty() && !LabelOf(file).Origin.empty();

			if (originIsHidden && file.Change == Core::FileChange::Renamed)
			{
				return std::format("Renamed from {}", Core::ToGenericUtf8(file.OldPath));
			}

			if (originIsHidden && file.Change == Core::FileChange::Copied)
			{
				return std::format("Copied from {}", Core::ToGenericUtf8(file.OldPath));
			}

			const char* changeName = ChangeName(file.Change);
			return changeName != nullptr ? std::string{changeName} : std::string{};
		}

		FileRowActions DrawFileRow(const Core::FileDiff& file, const PathLabel& label, const FileRowLook& look,
								   const GitPalette& palette)
		{
			FileRowActions actions;

			const ImVec2 rowStart = ImGui::GetCursorPos();
			const float rowRight = ListRowEndPosX();

			actions.Selected = ListRowSelectable("##row", look.IsSelected, ImGuiSelectableFlags_AllowOverlap);

			{
				const ListPopupScope popup;

				if (Core::BeginRightClickPopup("file-menu"))
				{
					const char* noShortcut = nullptr;
					const bool notChecked = false;

					actions.OpenInEditor = ImGui::MenuItem("Open in Editor", noShortcut, notChecked, look.CanOpen);
					actions.CopyPath = ImGui::MenuItem("Copy Path");
					ImGui::EndPopup();
				}
			}

			ImGui::SetCursorPos(ImVec2(rowStart.x + look.Indent, ListRowTextTop(rowStart.y)));

			switch (look.Marks)
			{
			case RowMarks::Shown:
			{
				// WARNING: A tick box is a frame high, and the row a line of text; with no frame padding the two agree,
				// and the letter after it stays on the row's text line.
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
				bool viewedChecked = look.Viewed == ViewedState::Viewed;
				actions.ToggledViewed = DrawViewedBox(&viewedChecked, look.Viewed, palette);
				ImGui::PopStyleVar();
				ImGui::SameLine();
				break;
			}
			case RowMarks::Hidden:
				break;
			}

			const bool viewed = look.Viewed == ViewedState::Viewed;
			const NameTone tone = viewed ? NameTone::Dimmed : NameTone::Bright;
			const ImU32 letterColour =
				viewed ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ChangeColor(file.Change, palette);

			const std::string letter(1, ChangeLetter(file.Change));
			ImGui::PushStyleColor(ImGuiCol_Text, letterColour);
			ImGui::TextUnformatted(letter.c_str());
			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				const std::string tooltip = ChangeTooltip(file, label);

				if (!tooltip.empty())
				{
					ImGui::SetTooltip("%s", tooltip.c_str());
				}
			}

			const Core::LineCounts& counts = look.Counts;
			const std::string countsText = file.IsBinary ? std::string{"binary"} : CountsText(counts);
			const bool changedSinceViewed = look.Viewed == ViewedState::ChangedSinceViewed;
			const std::string changedText = changedSinceViewed ? std::string{"changed"} : std::string{};
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float trailingWidth =
				Core::TextWidth(countsText) + (changedSinceViewed ? Core::TextWidth(changedText) + spacing : 0.0f);

			ImGui::SameLine();
			DrawPathLabel(label, rowRight - trailingWidth - spacing - ImGui::GetCursorPosX(), tone);

			ImGui::SameLine();
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - trailingWidth));

			if (changedSinceViewed)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, palette.GutterModified);
				ImGui::TextUnformatted(changedText.c_str());
				ImGui::PopStyleColor();
				ImGui::SetItemTooltip("Changed since you marked it viewed");
				ImGui::SameLine();
			}

			ImGui::TextDisabled("%s", countsText.c_str());

			return actions;
		}

		bool IsRowOfSelection(const ReviewTreeRow& row, const ReviewGroups& groups, const ReviewSelection& selected)
		{
			return row.Kind == TreeRowKind::File && row.Group == selected.Group &&
				   ShownPath(FileOfTreeRow(groups, row)) == selected.Path;
		}

		std::optional<std::size_t> RowOfSelection(const std::optional<ReviewSelection>& selected,
												  const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows)
		{
			if (!selected.has_value())
			{
				return std::nullopt;
			}

			for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
			{
				if (IsRowOfSelection(rows[rowIndex], groups, *selected))
				{
					return rowIndex;
				}
			}

			return std::nullopt;
		}

		void DrawListToolbar(std::span<char> filter, ReviewSessions& sessions, const std::string& branch,
							 RowMarks marks)
		{
			const SessionSettings settings = ReadSettings(sessions, branch);
			const int buttonCount = marks == RowMarks::Shown ? kButtonsWithMarks : kButtonsWithoutMarks;

			DrawFilterField("##reviewFilter", filter.data(), filter.size(),
							{.Hint = "Filter files", .Width = FilterFieldWidthBefore(buttonCount)});

			const bool isTree = settings.Shape == ListShape::Tree;
			const bool notEngaged = false;

			ImGui::SameLine();

			if (Core::DrawIconButton("##reviewShape", isTree ? ICON_LC_FOLDER_TREE : ICON_LC_LIST, notEngaged,
							   isTree ? "Show as a list" : "Show as a tree"))
			{
				SessionSettings changed = settings;
				changed.Shape = isTree ? ListShape::Flat : ListShape::Tree;
				sessions.SetSettings(branch, changed);
			}

			if (marks == RowMarks::Hidden)
			{
				return;
			}

			ImGui::SameLine();

			const bool hideViewedClicked =
				Core::DrawIconButton("##reviewHideViewed", ICON_LC_EYE_OFF, settings.HideViewed, "Hide viewed files");

			if (hideViewedClicked)
			{
				SessionSettings changed = settings;
				changed.HideViewed = !settings.HideViewed;
				sessions.SetSettings(branch, changed);
			}
		}

		void DrawCommitsTooltip(const ReviewRange& range)
		{
			if (!ImGui::BeginItemTooltip())
			{
				return;
			}

			ImGui::TextUnformatted(RangeSummary(range).c_str());

			const std::size_t shown = std::min(range.Commits.size(), kCommitsInTooltip);

			for (std::size_t commitIndex = 0; commitIndex < shown; ++commitIndex)
			{
				const Core::CommitSummary& commit = range.Commits[commitIndex];
				Core::PushCodeFont(Core::kInterfaceFontSize);
				ImGui::TextDisabled("%s", commit.Id.ShortHex().c_str());
				ImGui::PopFont();
				ImGui::SameLine();
				ImGui::TextUnformatted(commit.Summary.c_str());
			}

			if (range.Commits.size() > shown)
			{
				ImGui::TextDisabled("and %zu more", range.Commits.size() - shown);
			}

			ImGui::EndTooltip();
		}

		void DrawCommitTooltip(const Core::CommitSummary& commit)
		{
			const ListPopupScope tooltip;

			if (!ImGui::BeginItemTooltip())
			{
				return;
			}

			const std::string author = std::format("{} <{}>", commit.Author.Name, commit.Author.Email);
			ImGui::TextUnformatted(author.c_str());

			ImGui::PushTextWrapPos(ImGui::GetFontSize() * kCommitTooltipWrapEms);
			ImGui::TextUnformatted(commit.Summary.c_str());

			if (!commit.Body.empty())
			{
				ImGui::TextUnformatted(commit.Body.c_str());
			}

			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		bool DrawCommitRow(const Core::CommitSummary& commit, bool isPicked, std::chrono::sys_seconds now)
		{
			const ImVec2 rowStart = ImGui::GetCursorPos();
			const float rowRight = ListRowEndPosX();
			const bool clicked = ListRowSelectable("##commit", isPicked, ImGuiSelectableFlags_AllowOverlap);

			DrawCommitTooltip(commit);

			ImGui::SetCursorPos(ImVec2(rowStart.x, ListRowTextTop(rowStart.y)));
			Core::PushCodeFont(Core::kInterfaceFontSize);
			ImGui::TextDisabled("%s", commit.Id.ShortHex().c_str());
			ImGui::PopFont();
			ImGui::SameLine();

			const std::string age = RelativeAge(commit.Author.When, now);
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float summaryRoom = rowRight - Core::TextWidth(age) - spacing - ImGui::GetCursorPosX();
			ImGui::TextUnformatted(FitToWidth(commit.Summary, summaryRoom).c_str());

			DrawAtRight(age, rowRight);

			return clicked;
		}

		const Core::CommitSummary* PickedCommitIn(const ReviewRange& range, const std::optional<Core::Oid>& picked)
		{
			if (!picked.has_value())
			{
				return nullptr;
			}

			const std::optional<std::size_t> commitIndex = IndexOfCommit(range.Commits, *picked);
			return commitIndex.has_value() ? &range.Commits[*commitIndex] : nullptr;
		}

		RangeDiffKey CommitDiffKey(const Core::CommitSummary& commit, const SessionSettings& settings)
		{
			return RangeDiffKey{.Ends = EndsOfCommit(commit), .Whitespace = settings.Whitespace};
		}

		SinceViewedDiff SinceViewedDiffOf(const ReviewModel& model, const SinceViewedRequest& request)
		{
			if (model.IsSinceViewedUnavailable(request))
			{
				return SinceViewedDiff::ViewedVersionGone;
			}

			if (model.IsSinceViewedFailed(request))
			{
				return SinceViewedDiff::Unreadable;
			}

			return SinceViewedDiff::Available;
		}

		ChangedFileView ShownViewOf(const std::optional<SinceViewedRequest>& sinceViewed, SinceViewedDiff sinceViewedDiff,
									const SessionSettings& settings)
		{
			const bool canShowSinceViewed = sinceViewed.has_value() && sinceViewedDiff == SinceViewedDiff::Available;
			return canShowSinceViewed ? settings.ChangedFiles : ChangedFileView::AllChanges;
		}

		void SetChangedFiles(ReviewSessions& sessions, const std::string& branch, const SessionSettings& settings,
							 ChangedFileView view)
		{
			SessionSettings changed = settings;
			changed.ChangedFiles = view;
			sessions.SetSettings(branch, changed);
		}

		void DrawChangedFileSwitch(ReviewSessions& sessions, const std::string& branch, const SessionSettings& settings,
								   ChangedFileView shownView, SinceViewedDiff sinceViewedDiff)
		{
			if (ImGui::RadioButton("All changes", shownView == ChangedFileView::AllChanges))
			{
				SetChangedFiles(sessions, branch, settings, ChangedFileView::AllChanges);
			}

			ImGui::SameLine();

			const bool viewedVersionIsGone = sinceViewedDiff == SinceViewedDiff::ViewedVersionGone;
			ImGui::BeginDisabled(viewedVersionIsGone);

			if (ImGui::RadioButton("Since you viewed", shownView == ChangedFileView::SinceViewed))
			{
				SetChangedFiles(sessions, branch, settings, ChangedFileView::SinceViewed);
			}

			ImGui::EndDisabled();

			if (viewedVersionIsGone && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("The version you viewed is no longer available");
			}

			if (sinceViewedDiff == SinceViewedDiff::Unreadable)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", kSinceViewedUnreadable);
			}
		}

		bool OnlyBaseMoved(const ReviewSessions& sessions, const std::string& branch, ReviewGroup group,
						   const Core::FileDiff& file)
		{
			if (sessions.StateOf(branch, group, file) != ViewedState::ChangedSinceViewed)
			{
				return false;
			}

			const std::optional<ViewedMark> mark = sessions.MarkOf(branch, group, file);
			return mark.has_value() && mark->New == file.NewId;
		}

		void DrawDiffPaneWithoutFile(DiffPaneWithoutFile pane)
		{
			switch (pane)
			{
			case DiffPaneWithoutFile::SelectAFile:
				Core::EmptyState("Select a file to see its changes.");
				return;
			case DiffPaneWithoutFile::NothingToReview:
				Core::EmptyState("Nothing to review.");
				return;
			case DiffPaneWithoutFile::LoadingUncommitted:
				Core::LoadingState(kLoadingUncommitted);
				return;
			}
		}

		void DrawHideWhitespace(ReviewSessions& sessions, const std::string& branch, const SessionSettings& settings)
		{
			const char* label = "Hide whitespace";
			const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			const float width = TickBoxWidth(label);

			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - width));

			bool hidden = settings.Whitespace == Core::WhitespaceInDiff::Ignored;

			if (DrawTickBox(label, &hidden))
			{
				SessionSettings changed = settings;
				changed.Whitespace = hidden ? Core::WhitespaceInDiff::Ignored : Core::WhitespaceInDiff::Shown;
				sessions.SetSettings(branch, changed);
			}
		}
	}

	void ReviewWindows::RenderTab(Tab& tab, const ReviewRenderInput& input)
	{
		const ReviewSession* session = input.Sessions.Find(tab.Branch);

		if (session == nullptr || !tab.Key.has_value())
		{
			return;
		}

		const ReviewKey key = *tab.Key;
		const SessionSettings settings = session->Settings;
		const RowsCache* noRowsYet = nullptr;

		input.Model.RequestRange(key);
		const Core::Ref<const ReviewRange> range = input.Model.GetRange(key);
		const bool rangeLoading = input.Model.IsRangeLoading(key);

		if (range == nullptr)
		{
			RenderHeader(tab, input, nullptr, noRowsYet, kNotUpdating);

			const Core::Error* error = input.Model.GetRangeError(key);

			if (error == nullptr || input.Model.IsRangeRetrying(key))
			{
				Core::LoadingState("Loading the review");
				return;
			}

			if (Core::EmptyState(error->Message, "Retry"))
			{
				input.Model.Retry(key);
			}

			return;
		}

		if (range->Problem != RangeProblem::None)
		{
			RenderHeader(tab, input, range.get(), noRowsYet, kNotUpdating);

			const std::string problem = RangeProblemMessage(range->Problem, tab.Branch, session->Base);

			if (range->Problem == RangeProblem::BranchMissing)
			{
				if (Core::EmptyState(problem, "Close"))
				{
					tab.CloseRequested = true;
				}

				return;
			}

			if (Core::EmptyState(problem, "End review"))
			{
				AskToEnd(tab.Branch);
			}

			return;
		}

		if (range->Worktree.has_value())
		{
			tab.LastWorktree = range->Worktree;
		}
		else if (settings.IncludeUncommitted)
		{
			input.Model.StopUncommitted(key);
		}

		const RangeDiffKey diffKey{.Ends = range->Ends, .Whitespace = settings.Whitespace};
		input.Model.RequestCommittedDiff(diffKey);

		const bool showUncommitted = settings.IncludeUncommitted && range->Worktree.has_value();

		if (showUncommitted)
		{
			input.Model.RequestUncommitted(key, *range->Worktree, settings.Whitespace);
		}

		const LoadedReview review{.Key = key,
								  .Range = range,
								  .Committed = input.Model.GetCommittedDiff(diffKey),
								  .Uncommitted = showUncommitted ? input.Model.GetUncommitted(key) : nullptr};
		ReviewRefresh refresh{.RangeLoading = rangeLoading,
							  .UncommittedLoading = showUncommitted && input.Model.IsUncommittedLoading(key),
							  .UncommittedFailed = showUncommitted && input.Model.GetUncommittedError(key) != nullptr,
							  .UncommittedRetrying = showUncommitted && input.Model.IsUncommittedRetrying(key)};

		if (review.Committed != nullptr)
		{
			tab.LastLoaded = review;
			refresh.ShowsEarlierChanges = false;
			refresh.UncommittedShown = review.Uncommitted != nullptr;
			RenderBody(tab, input, review, refresh);
			return;
		}

		const bool failed = input.Model.IsCommittedDiffFailed(diffKey);

		if (!failed && tab.LastLoaded.has_value())
		{
			const LoadedReview previous = *tab.LastLoaded;
			refresh.ShowsEarlierChanges = true;
			refresh.UncommittedShown = previous.Uncommitted != nullptr;
			RenderBody(tab, input, previous, refresh);
			return;
		}

		RenderHeader(tab, input, range.get(), noRowsYet, kNotUpdating);

		if (!failed)
		{
			Core::LoadingState("Loading the changes");
			return;
		}

		const std::optional<Core::Error>& diffError = input.Model.GetCommittedDiffError();
		const std::string message = diffError.has_value() ? diffError->Message : std::string{kDiffUnreadable};

		if (Core::EmptyState(message, "Retry"))
		{
			input.Model.Retry(key);
		}
	}

	void ReviewWindows::RenderHeader(Tab& tab, const ReviewRenderInput& input, const ReviewRange* range,
									 const RowsCache* rows, bool updating)
	{
		const ReviewSession* session = input.Sessions.Find(tab.Branch);
		const SessionSettings settings = session != nullptr ? session->Settings : SessionSettings{};

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Core::ShortBranchName(tab.Branch).c_str());

		if (session != nullptr)
		{
			const float noOffset = 0.0f;
			const float noSpacing = 0.0f;

			ImGui::SameLine(noOffset, noSpacing);
			ImGui::TextDisabled(" against %s", Core::ShortBranchName(session->Base).c_str());
		}

		const bool changeSummaryIsKnown = rows != nullptr && range != nullptr && range->Problem == RangeProblem::None;

		if (changeSummaryIsKnown)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", rows->Summary.c_str());
			DrawCommitsTooltip(*range);
		}

		ImGui::SameLine();

		const bool canInclude = range != nullptr && range->Worktree.has_value();
		bool include = settings.IncludeUncommitted && canInclude;

		ImGui::BeginDisabled(!canInclude);

		if (DrawTickBox("Include uncommitted", &include))
		{
			SetIncludeUncommitted(tab, input.Sessions, input.Model, include);
		}

		ImGui::EndDisabled();

		if (range != nullptr && !range->Worktree.has_value())
		{
			ImGui::SetItemTooltip("Not checked out in any worktree");
		}

		if (updating)
		{
			DrawHeaderLoader(kUpdatingChanges);
		}

		if (rows != nullptr)
		{
			const std::string viewedText = std::format("{}/{} viewed", rows->Viewed.Viewed, rows->Viewed.Total);

			ImGui::SameLine();
			const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - Core::TextWidth(viewedText)));
			ImGui::TextDisabled("%s", viewedText.c_str());
		}
	}

	void ReviewWindows::RenderBody(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
								   const ReviewRefresh& refresh)
	{
		RefreshRows(tab, input.Sessions, ReadSettings(input.Sessions, tab.Branch), review, IsUncommittedAwaited(refresh));
		FollowCommits(tab, *review.Range);

		const ReviewGroups groups = GroupsOf(tab, review);
		const OpenRelativeFile openFile = OpenFileIn(*review.Range);

		RenderHeader(tab, input, review.Range.get(), &tab.Rows, IsUpdatingChanges(refresh));

		const bool alreadyMerged = review.Range->Ends.MergeBase == review.Range->Ends.Tip;

		if (alreadyMerged)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", AlreadyMergedMessage(tab.Branch, review.Key.Base).c_str());
			ImGui::SameLine();

			if (Core::SmallTextButton("End review"))
			{
				AskToEnd(tab.Branch);
			}
		}

		const bool includeUncommitted = ReadSettings(input.Sessions, tab.Branch).IncludeUncommitted;
		const Core::Error* uncommittedError =
			ShowsUncommittedFailure(refresh) ? input.Model.GetUncommittedError(review.Key) : nullptr;

		if (uncommittedError != nullptr && review.Range->Worktree.has_value())
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", UncommittedProblem(*uncommittedError, *review.Range->Worktree).c_str());
			ImGui::SameLine();

			if (Core::SmallTextButton("Retry"))
			{
				input.Model.Retry(review.Key);
			}
		}

		const bool worktreeGone = includeUncommitted && !review.Range->Worktree.has_value() && tab.LastWorktree.has_value();

		if (worktreeGone)
		{
			ImGui::TextDisabled("%s", std::format("The worktree at {} is gone.", Core::ToUtf8(*tab.LastWorktree)).c_str());
		}

		if (ImGui::BeginTable("review", kReviewColumnCount, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * kFileColumnEms);
			ImGui::TableSetupColumn("diff", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			RenderPages(tab, input, review, groups, openFile);

			ImGui::TableSetColumnIndex(1);
			RenderDiffColumn(tab, input, review, groups, openFile);

			ImGui::EndTable();
		}
	}

	void ReviewWindows::RenderPages(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
									const ReviewGroups& groups, const OpenRelativeFile& openFile)
	{
		if (ImGui::BeginTabBar("##reviewPages"))
		{
			if (ImGui::BeginTabItem("Files"))
			{
				tab.Page = ReviewPage::Files;
				DrawListToolbar(tab.Filter, input.Sessions, tab.Branch, RowMarks::Shown);
				RenderFileList(tab, input, FilesPageList(tab, groups), openFile);
				ImGui::EndTabItem();
			}

			// WARNING: The label carries the commit count and changes with it; the identity after ### keeps the tab the same one.
			const std::string commitsLabel = CommitsTabLabel(*review.Range) + "###commits";

			if (ImGui::BeginTabItem(commitsLabel.c_str()))
			{
				tab.Page = ReviewPage::Commits;
				RenderCommitsPage(tab, input, *review.Range, openFile);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}

	void ReviewWindows::RenderFileList(Tab& tab, const ReviewRenderInput& input, const FileList& list,
									   const OpenRelativeFile& openFile)
	{
		const ImVec2 fillTheRest{0.0f, 0.0f};

		// WARNING: NoNavInputs keeps ImGui's keyboard navigation from toggling a checkbox on Space. AlwaysUseWindowPadding is
		// needed because ImGui gives a child without a border no padding.
		PushListPadding();
		const bool listShown =
			ImGui::BeginChild(list.ChildId, fillTheRest, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNavInputs);
		ImGui::PopStyleVar();

		if (listShown)
		{
			const ListBodyScope listBody;
			ImGui::Indent(ListRowEndInset());

			const std::vector<ReviewTreeRow>& rows = list.Rows.Rows;

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()), ListRowPitch());

			if (list.ScrollToSelected)
			{
				const std::optional<std::size_t> selectedRow = RowOfSelection(list.Selected, list.Groups, rows);

				if (selectedRow.has_value())
				{
					clipper.IncludeItemByIndex(static_cast<int>(*selectedRow));
				}
				else
				{
					list.ScrollToSelected = false;
				}
			}

			while (clipper.Step())
			{
				for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
				{
					const ReviewTreeRow& row = rows[static_cast<std::size_t>(rowIndex)];

					ImGui::PushID(static_cast<int>(IndexOf(row.Group)));
					RenderTreeRow(tab, input, list, row, openFile);
					ImGui::PopID();
				}
			}

			if (rows.empty() && !list.Rows.Filter.empty())
			{
				ImGui::TextDisabled("No files match.");
			}

			HandleListKeys(tab, input.Sessions, list);
			ImGui::Unindent(ListRowEndInset());
		}

		ImGui::EndChild();
	}

	void ReviewWindows::RenderTreeRow(Tab& tab, const ReviewRenderInput& input, const FileList& list,
									  const ReviewTreeRow& row, const OpenRelativeFile& openFile)
	{
		switch (row.Kind)
		{
		case TreeRowKind::Heading:
			if (DrawHeadingRow(row, HeadingNote(row, list.Rows.Filter), list.Rows.GroupCounts[IndexOf(row.Group)]))
			{
				bool& collapsed = tab.Collapsed[IndexOf(row.Group)];
				collapsed = !collapsed;
			}

			return;
		case TreeRowKind::Folder:
		{
			const float indent = static_cast<float>(row.Depth) * ImGui::GetStyle().IndentSpacing;

			ImGui::PushID(Core::ToUtf8(row.FolderPath).c_str());
			const bool clicked = DrawFolderRow(row, indent, list.Marks);
			ImGui::PopID();

			const bool listIsNarrowed = !list.Rows.Filter.empty() || list.Rows.HideViewed;

			if (clicked && !listIsNarrowed)
			{
				ToggleFolder(list.Folded, row);
				++list.FoldsVersion;
			}

			return;
		}
		case TreeRowKind::File:
			RenderFileRow(tab, input, list, row, openFile);
			return;
		case TreeRowKind::Loading:
			DrawLoadingRow(row);
			return;
		}
	}

	void ReviewWindows::RenderFileRow(Tab& tab, const ReviewRenderInput& input, const FileList& list,
									  const ReviewTreeRow& row, const OpenRelativeFile& openFile)
	{
		const Core::FileDiff& file = FileOfTreeRow(list.Groups, row);
		const std::filesystem::path& path = ShownPath(file);
		const bool fileIsOnDisk = file.Change != Core::FileChange::Deleted;
		const bool isSelected = list.Selected.has_value() && IsRowOfSelection(row, list.Groups, *list.Selected);
		const bool showsMarks = list.Marks == RowMarks::Shown;
		const ViewedState viewed =
			showsMarks ? input.Sessions.StateOf(tab.Branch, row.Group, file) : ViewedState::NotViewed;

		ImGui::PushID(Core::ToUtf8(path).c_str());

		const FileRowLook look{.IsSelected = isSelected,
							   .Marks = list.Marks,
							   .Viewed = viewed,
							   .CanOpen = openFile != nullptr && fileIsOnDisk,
							   .Indent = static_cast<float>(row.Depth) * ImGui::GetStyle().IndentSpacing,
							   .Counts = list.Rows.FileCounts[IndexOf(row.Group)][row.FileIndex]};

		const FileRowActions actions = DrawFileRow(file, LabelInShape(file, list.Rows.Shape), look, input.Palette);

		ImGui::PopID();

		if (isSelected && list.ScrollToSelected)
		{
			if (!ImGui::IsItemVisible())
			{
				ImGui::SetScrollHereY();
			}

			list.ScrollToSelected = false;
		}

		if (actions.Selected)
		{
			list.Selected = ReviewSelection{.Group = row.Group, .Path = path};
		}

		if (actions.ToggledViewed)
		{
			ToggleViewed(tab, input.Sessions, row.Group, file);
		}

		if (actions.OpenInEditor && openFile)
		{
			openFile(path);
		}

		if (actions.CopyPath && CopyText)
		{
			CopyText("path", Core::ToGenericUtf8(path));
		}
	}

	void ReviewWindows::HandleListKeys(Tab& tab, ReviewSessions& sessions, const FileList& list)
	{
		if (!ImGui::IsWindowFocused())
		{
			return;
		}

		const std::vector<ReviewTreeRow>& rows = list.Rows.Rows;

		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
		{
			list.Selected = NeighbourFileOf(list.Selected, list.Groups, rows, ListStep::Next, list.Rows.Shape);
			list.ScrollToSelected = true;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
		{
			list.Selected = NeighbourFileOf(list.Selected, list.Groups, rows, ListStep::Previous, list.Rows.Shape);
			list.ScrollToSelected = true;
		}

		const bool noRepeat = false;
		const bool listHasMarks = list.Marks == RowMarks::Shown;

		if (!listHasMarks || !ImGui::IsKeyPressed(ImGuiKey_Space, noRepeat) || !list.Selected.has_value())
		{
			return;
		}

		const Core::FileDiff* file = SelectedFile(list.Groups, *list.Selected);

		if (file != nullptr)
		{
			ToggleViewed(tab, sessions, list.Selected->Group, *file);
		}
	}

	void ReviewWindows::RenderCommitsPage(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
										  const OpenRelativeFile& openFile)
	{
		const std::function<void()> drawCommits = [this, &tab, &range] { RenderCommitList(tab, range); };
		const std::function<void()> drawPicked = [this, &tab, &input, &range, &openFile] { RenderPickedCommit(tab, input, range, openFile); };

		DrawSplitResults(SplitResultsInput{.HasSelection = tab.SelectedCommit.has_value(),
										   .ResultsHeight = tab.CommitListHeight,
										   .DrawResults = drawCommits,
										   .DrawPreview = drawPicked,
										   .ResultsChildId = "##reviewCommits",
										   .PreviewChildId = "##reviewCommit",
										   .SplitterId = "##reviewCommitSplit"});
	}

	void ReviewWindows::RenderCommitList(Tab& tab, const ReviewRange& range)
	{
		const ImVec2 fillThePane{0.0f, 0.0f};

		PushListPadding();
		const bool listShown = ImGui::BeginChild("##reviewCommitRows", fillThePane, ImGuiChildFlags_AlwaysUseWindowPadding,
												 ImGuiWindowFlags_NoNavInputs);
		ImGui::PopStyleVar();

		if (listShown)
		{
			const ListBodyScope listBody;

			if (range.Commits.empty())
			{
				Core::EmptyState("No commits.");
			}
			else
			{
				ImGui::Indent(ListRowEndInset());
				RenderCommitRows(tab, range);
				ImGui::Unindent(ListRowEndInset());
			}

			HandleCommitKeys(tab, range);
		}

		ImGui::EndChild();
	}

	void ReviewWindows::RenderCommitRows(Tab& tab, const ReviewRange& range)
	{
		const std::vector<Core::CommitSummary>& commits = range.Commits;
		const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(commits.size()), ListRowPitch());

		if (tab.ScrollToCommit)
		{
			const std::optional<std::size_t> pickedRow =
				tab.SelectedCommit.has_value() ? IndexOfCommit(commits, *tab.SelectedCommit) : std::nullopt;

			if (pickedRow.has_value())
			{
				clipper.IncludeItemByIndex(static_cast<int>(*pickedRow));
			}
			else
			{
				tab.ScrollToCommit = false;
			}
		}

		while (clipper.Step())
		{
			for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
			{
				const Core::CommitSummary& commit = commits[static_cast<std::size_t>(rowIndex)];
				const bool isPicked = tab.SelectedCommit == commit.Id;

				ImGui::PushID(rowIndex);
				const bool clicked = DrawCommitRow(commit, isPicked, now);
				ImGui::PopID();

				if (isPicked && tab.ScrollToCommit)
				{
					if (!ImGui::IsItemVisible())
					{
						ImGui::SetScrollHereY();
					}

					tab.ScrollToCommit = false;
				}

				if (clicked)
				{
					PickCommit(tab, commit.Id);
				}
			}
		}

		if (!range.CommitsComplete)
		{
			ImGui::TextDisabled("Only the newest %zu commits are listed.", commits.size());
		}
	}

	void ReviewWindows::HandleCommitKeys(Tab& tab, const ReviewRange& range)
	{
		if (!ImGui::IsWindowFocused())
		{
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
		{
			PickCommit(tab, NeighbourCommit(tab.SelectedCommit, range.Commits, ListStep::Next));
			tab.ScrollToCommit = true;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
		{
			PickCommit(tab, NeighbourCommit(tab.SelectedCommit, range.Commits, ListStep::Previous));
			tab.ScrollToCommit = true;
		}
	}

	void ReviewWindows::RenderPickedCommit(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
										   const OpenRelativeFile& openFile)
	{
		const Core::CommitSummary* commit = PickedCommitIn(range, tab.SelectedCommit);

		if (commit == nullptr)
		{
			return;
		}

		const SessionSettings settings = ReadSettings(input.Sessions, tab.Branch);
		const RangeDiffKey diffKey = CommitDiffKey(*commit, settings);

		input.Model.RequestCommittedDiff(diffKey);
		const Core::Ref<const Core::Diff> commitDiff = input.Model.GetCommittedDiff(diffKey);

		if (commitDiff != nullptr)
		{
			RefreshCommitRows(tab, settings, *commit, commitDiff);
		}

		// WARNING: The line with its counts is made with the rows, once per diff; counting every line of a large commit on
		// every frame would cost the frame.
		const std::string line = commitDiff != nullptr ? tab.CommitRows.Summary : CommitLine(*commit, nullptr);
		constexpr Core::IconButtonFit kOnTheTextLine = Core::IconButtonFit::Line;

		ImGui::TextUnformatted(FitToWidth(line, ImGui::GetContentRegionAvail().x - PaneCloseRoom(kOnTheTextLine)).c_str());

		if (DrawPaneClose(kOnTheTextLine))
		{
			PickCommit(tab, std::nullopt);
			return;
		}

		DrawListToolbar(tab.Filter, input.Sessions, tab.Branch, RowMarks::Hidden);

		if (commitDiff == nullptr)
		{
			DrawDiffNotLoaded(input.Model.IsCommittedDiffFailed(diffKey));
			return;
		}

		RenderFileList(tab, input, CommitFileList(tab, *commitDiff), openFile);
	}

	void ReviewWindows::RenderDiffColumn(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
										 const ReviewGroups& groups, const OpenRelativeFile& openFile)
	{
		switch (tab.Page)
		{
		case ReviewPage::Files:
			RenderSelectedDiff(tab, input, review, groups, openFile);
			return;
		case ReviewPage::Commits:
			RenderCommitFileDiff(tab, input, *review.Range, openFile);
			return;
		}
	}

	void ReviewWindows::RenderSelectedDiff(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
										   const ReviewGroups& groups, const OpenRelativeFile& openFile)
	{
		const SessionSettings settings = ReadSettings(input.Sessions, tab.Branch);
		const Core::FileDiff* file = tab.Selected.has_value() ? SelectedFile(groups, *tab.Selected) : nullptr;
		const std::optional<SinceViewedRequest> sinceViewed =
			file != nullptr ? SinceViewedRequestFor(input.Sessions, tab.Branch, tab.Selected->Group, *file,
													*review.Range, settings.Whitespace)
							: std::nullopt;
		const SinceViewedDiff sinceViewedDiff =
			sinceViewed.has_value() ? SinceViewedDiffOf(input.Model, *sinceViewed) : SinceViewedDiff::Available;
		const ChangedFileView shownView = ShownViewOf(sinceViewed, sinceViewedDiff, settings);

		if (sinceViewed.has_value())
		{
			DrawChangedFileSwitch(input.Sessions, tab.Branch, settings, shownView, sinceViewedDiff);
			ImGui::SameLine();
		}
		else if (file != nullptr && OnlyBaseMoved(input.Sessions, tab.Branch, tab.Selected->Group, *file))
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Only the base moved since you viewed this file.");
			ImGui::SameLine();
		}

		DrawHideWhitespace(input.Sessions, tab.Branch, settings);

		if (ImGui::BeginChild("##reviewDiff"))
		{
			const bool showsSinceViewed = shownView == ChangedFileView::SinceViewed && sinceViewed.has_value();

			if (file == nullptr)
			{
				DrawDiffPaneWithoutFile(DiffPaneWithoutFileFor(tab.Rows.Viewed.Total, tab.Rows.UncommittedLoading));
			}
			else if (showsSinceViewed)
			{
				RenderSinceViewed(tab, input, *sinceViewed, openFile);
			}
			else if (OnlyWhitespaceChanged(*file, settings.Whitespace))
			{
				Core::EmptyState(kOnlyWhitespace);
			}
			else
			{
				UpdateTexts(tab, input.Model, review, *file);
				RenderFileDiff(tab, input, *file, openFile);
			}
		}

		ImGui::EndChild();
	}

	void ReviewWindows::RenderSinceViewed(Tab& tab, const ReviewRenderInput& input, const SinceViewedRequest& request,
										  const OpenRelativeFile& openFile)
	{
		input.Model.RequestSinceViewed(request);
		const Core::Ref<const SinceViewedAnswer> answer = input.Model.GetSinceViewed(request);

		if (answer == nullptr)
		{
			Core::LoadingState("Loading the changes since you last looked");
			return;
		}

		if (OnlyWhitespaceChanged(answer->Diff, request.Whitespace))
		{
			Core::EmptyState(kOnlyWhitespace);
			return;
		}

		// WARNING: The texts alias the answer rather than being copied, because HoldTexts compares the pointer and it must
		// change only when the answer does.
		HoldTexts(tab, Core::Ref<const FileTexts>(answer, &answer->Texts));
		RenderFileDiff(tab, input, answer->Diff, openFile);
	}

	void ReviewWindows::RenderCommitFileDiff(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
											 const OpenRelativeFile& openFile)
	{
		const SessionSettings settings = ReadSettings(input.Sessions, tab.Branch);

		DrawHideWhitespace(input.Sessions, tab.Branch, settings);

		if (ImGui::BeginChild("##reviewDiff"))
		{
			RenderPickedFileDiff(tab, input, range, settings, openFile);
		}

		ImGui::EndChild();
	}

	void ReviewWindows::RenderPickedFileDiff(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
											 const SessionSettings& settings, const OpenRelativeFile& openFile)
	{
		const Core::CommitSummary* commit = PickedCommitIn(range, tab.SelectedCommit);

		if (commit == nullptr)
		{
			Core::EmptyState("Select a commit to see its changes.");
			return;
		}

		const RangeDiffKey diffKey = CommitDiffKey(*commit, settings);

		input.Model.RequestCommittedDiff(diffKey);
		const Core::Ref<const Core::Diff> commitDiff = input.Model.GetCommittedDiff(diffKey);

		if (commitDiff == nullptr)
		{
			DrawDiffNotLoaded(input.Model.IsCommittedDiffFailed(diffKey));
			return;
		}

		const ReviewGroups groups{.Commits = commitDiff.get()};
		const Core::FileDiff* file =
			tab.SelectedCommitFile.has_value() ? SelectedFile(groups, *tab.SelectedCommitFile) : nullptr;

		if (file == nullptr)
		{
			const bool commitHasFiles = !commitDiff->Files.empty();
			Core::EmptyState(commitHasFiles ? "Select a file to see its changes." : "This commit changes no files.");
			return;
		}

		if (OnlyWhitespaceChanged(*file, settings.Whitespace))
		{
			Core::EmptyState(kOnlyWhitespace);
			return;
		}

		const RangeEnds ends = EndsOfCommit(*commit);

		input.Model.RequestCommittedTexts(ends, *file);
		HoldTexts(tab, input.Model.GetCommittedTexts(ends, *file));
		tab.View.SetTextsWait(input.Model.CommittedTextsWait(ends, *file));
		RenderFileDiff(tab, input, *file, openFile);
	}

	void ReviewWindows::RenderFileDiff(Tab& tab, const ReviewRenderInput& input, const Core::FileDiff& file,
									   const OpenRelativeFile& openFile)
	{
		tab.View.OnOpenFile = openFile;
		tab.View.OnCopied = OnCopied;
		tab.View.OnSetOptions = OnSetDiffOptions;
		tab.View.Render(file, input.Palette, input.Syntax, input.Selection, tab.Texts, tab.Tokens, input.DiffOptions);
	}
}

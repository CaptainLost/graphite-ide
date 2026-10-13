#include "Review/ReviewWindows.h"

#include "Git/History/CommitLabels.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr float kBaseFieldEms = 14.0f;
		constexpr float kFileColumnEms = 24.0f;
		constexpr int kReviewColumnCount = 2;
		constexpr std::size_t kCommitsInTooltip = 20;

		constexpr const char* kCommitsHeading = "Commits";
		constexpr const char* kUncommittedHeading = "Uncommitted in worktree";

		const char* HeadingOf(ReviewGroup group)
		{
			switch (group)
			{
			case ReviewGroup::Commits:
				return kCommitsHeading;
			case ReviewGroup::Uncommitted:
				return kUncommittedHeading;
			}

			return kCommitsHeading;
		}

		float MeasureText(std::string_view text)
		{
			return text.empty() ? 0.0f : ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
		}

		// CalcTextSize rounds each run up, so a cut measured against the room left gives that pixel back.
		std::string FittedText(const std::string& text, float room)
		{
			const float roundedUpPixel = 1.0f;
			const std::function<float(std::string_view)> widthOf = MeasureText;
			const std::optional<std::size_t> cut =
				CutToFit(text, room + roundedUpPixel, MeasureText(Ellipsis), widthOf);

			if (!cut.has_value())
			{
				return text;
			}

			return text.substr(0, *cut) + std::string{Ellipsis};
		}

		std::string UncommittedProblem(const Core::Error& error, const std::filesystem::path& worktree)
		{
			if (error.Code == Core::ErrorCode::NotFound)
			{
				return std::format("The worktree at {} is gone.", Core::ToUtf8(worktree));
			}

			return std::format("Uncommitted changes could not be read: {}", error.Message);
		}

		// Drawn over one Selectable as tall as the row, so the whole row folds the group.
		bool DrawGroupHeading(ReviewGroup group, std::size_t fileCount, bool collapsed, float rowHeight)
		{
			const ImVec2 rowStart = ImGui::GetCursorPos();
			const bool notSelected = false;
			const bool clicked =
				ImGui::Selectable("##heading", notSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight));

			ImGui::SetCursorPos(rowStart);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN);
			ImGui::SameLine();
			ImGui::TextUnformatted(std::format("{} ({})", HeadingOf(group), fileCount).c_str());

			return clicked;
		}

		void DrawPathLabel(const PathLabel& label, float room)
		{
			const std::string prefix = label.Origin + label.Directory;
			const float prefixRoom = room - MeasureText(label.Name);
			const bool prefixHasRoom = !prefix.empty() && prefixRoom > MeasureText(Ellipsis);

			float nameRoom = room;

			if (prefixHasRoom)
			{
				const std::string shownPrefix = FittedText(prefix, prefixRoom);
				const float noOffset = 0.0f;
				const float noSpacing = 0.0f;

				ImGui::TextDisabled("%s", shownPrefix.c_str());
				ImGui::SameLine(noOffset, noSpacing);
				nameRoom -= MeasureText(shownPrefix);
			}

			ImGui::TextUnformatted(FittedText(label.Name, nameRoom).c_str());
		}

		struct FileRowLook
		{
			bool IsSelected = false;
			ViewedState Viewed = ViewedState::NotViewed;
			bool CanOpen = false;
			float RowHeight = 0.0f;
			LineCounts Counts;
		};

		struct FileRowActions
		{
			bool Selected = false;
			bool ToggledViewed = false;
			bool OpenInEditor = false;
			bool CopyPath = false;
		};

		// The checkbox, the change letter, the path and - at the right edge - the line counts, laid
		// over one Selectable as tall as the row so a click anywhere else selects it.
		FileRowActions DrawFileRow(const Core::FileDiff& file, const FileRowLook& look, const GitPalette& palette)
		{
			FileRowActions actions;

			const ImVec2 rowStart = ImGui::GetCursorPos();
			const float rowRight = rowStart.x + ImGui::GetContentRegionAvail().x;

			actions.Selected = ImGui::Selectable("##row", look.IsSelected, ImGuiSelectableFlags_AllowOverlap,
												 ImVec2(0.0f, look.RowHeight));

			if (Core::BeginRightClickPopup("file-menu"))
			{
				const char* noShortcut = nullptr;
				const bool notChecked = false;

				actions.OpenInEditor = ImGui::MenuItem("Open in Editor", noShortcut, notChecked, look.CanOpen);
				actions.CopyPath = ImGui::MenuItem("Copy Path");
				ImGui::EndPopup();
			}

			ImGui::SetCursorPos(rowStart);

			bool viewed = look.Viewed == ViewedState::Viewed;
			actions.ToggledViewed = ImGui::Checkbox("##viewed", &viewed);

			ImGui::SameLine();
			const std::string letter(1, ChangeLetter(file.Change));
			ImGui::PushStyleColor(ImGuiCol_Text, ChangeColor(file.Change, palette));
			ImGui::TextUnformatted(letter.c_str());
			ImGui::PopStyleColor();

			if (const char* changeName = ChangeName(file.Change); changeName != nullptr)
			{
				ImGui::SetItemTooltip("%s", changeName);
			}

			const LineCounts& counts = look.Counts;
			const std::string countsText =
				file.IsBinary ? std::string{"binary"} : std::format("+{} -{}", counts.Added, counts.Removed);
			const bool changedSinceViewed = look.Viewed == ViewedState::ChangedSinceViewed;
			const std::string changedText = changedSinceViewed ? std::string{"changed"} : std::string{};
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float trailingWidth =
				MeasureText(countsText) + (changedSinceViewed ? MeasureText(changedText) + spacing : 0.0f);

			ImGui::SameLine();
			DrawPathLabel(LabelOf(file), rowRight - trailingWidth - spacing - ImGui::GetCursorPosX());

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

		bool HasFileRow(const std::vector<ReviewListRow>& rows)
		{
			for (const ReviewListRow& row : rows)
			{
				if (!row.IsHeading)
				{
					return true;
				}
			}

			return false;
		}

		std::optional<std::size_t> RowOfSelection(const std::optional<ReviewSelection>& selected,
												  const ReviewGroups& groups, const std::vector<ReviewListRow>& rows)
		{
			if (!selected.has_value())
			{
				return std::nullopt;
			}

			for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
			{
				const ReviewListRow& row = rows[rowIndex];

				if (!row.IsHeading && row.Group == selected->Group && ShownPath(FileOfRow(groups, row)) == selected->Path)
				{
					return rowIndex;
				}
			}

			return std::nullopt;
		}
	}

	void ReviewWindows::RenderTab(Tab& tab, const ReviewRenderInput& input)
	{
		const std::optional<ReviewKey> key = KeyOf(tab);

		if (!key.has_value())
		{
			RenderHeader(tab, input, nullptr, std::nullopt);
			Core::EmptyState("Pick a base to review this branch against.");
			return;
		}

		input.Model.RequestRange(*key);
		const Core::Ref<const ReviewRange> range = input.Model.GetRange(*key);

		if (range == nullptr)
		{
			RenderHeader(tab, input, nullptr, std::nullopt);

			const Core::Error* error = input.Model.GetRangeError(*key);

			if (error == nullptr)
			{
				Core::EmptyState("Loading…");
				return;
			}

			if (Core::EmptyState(error->Message, "Retry"))
			{
				input.Model.Retry(*key);
			}

			return;
		}

		if (range->Problem != RangeProblem::None)
		{
			RenderHeader(tab, input, range.get(), std::nullopt);

			const std::string problem = RangeProblemMessage(range->Problem, tab.Target.Branch, *tab.Target.Base);

			if (range->Problem == RangeProblem::BranchMissing)
			{
				if (Core::EmptyState(problem, "Close"))
				{
					tab.CloseRequested = true;
				}

				return;
			}

			Core::EmptyState(problem);
			return;
		}

		if (range->Worktree.has_value())
		{
			tab.LastWorktree = range->Worktree;
		}
		else if (tab.IncludeUncommitted)
		{
			input.Model.StopUncommitted(*key);
		}

		const RangeDiffKey diffKey{.Ends = range->Ends, .Whitespace = Core::WhitespaceInDiff::Shown};
		input.Model.RequestCommittedDiff(diffKey);

		const bool showUncommitted = tab.IncludeUncommitted && range->Worktree.has_value();

		if (showUncommitted)
		{
			input.Model.RequestUncommitted(*key, *range->Worktree, Core::WhitespaceInDiff::Shown);
		}

		const LoadedReview review{.Key = *key,
								  .Range = range,
								  .Committed = input.Model.GetCommittedDiff(diffKey),
								  .Uncommitted = showUncommitted ? input.Model.GetUncommitted(*key) : nullptr};

		if (review.Committed != nullptr)
		{
			tab.LastLoaded = review;
			RenderBody(tab, input, review);
			return;
		}

		const bool failed = input.Model.IsCommittedDiffFailed(diffKey);

		if (!failed && tab.LastLoaded.has_value())
		{
			const LoadedReview previous = *tab.LastLoaded;
			RenderBody(tab, input, previous);
			return;
		}

		RenderHeader(tab, input, range.get(), std::nullopt);

		if (!failed)
		{
			Core::EmptyState("Loading…");
			return;
		}

		const std::optional<Core::Error>& diffError = input.Model.GetCommittedDiffError();
		const std::string message = diffError.has_value() ? diffError->Message : std::string{"The diff could not be read."};

		if (Core::EmptyState(message, "Retry"))
		{
			input.Model.Retry(*key);
		}
	}

	void ReviewWindows::RenderHeader(Tab& tab, const ReviewRenderInput& input, const ReviewRange* range,
									 const std::optional<ViewedCount>& viewed)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Base");
		ImGui::SameLine();

		const bool baseMissing = range != nullptr && range->Problem == RangeProblem::BaseMissing;
		const std::string shownBase =
			tab.Target.Base.has_value() && !baseMissing ? ShortBranchName(*tab.Target.Base) : std::string{};

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * kBaseFieldEms);

		if (ImGui::BeginCombo("##base", shownBase.c_str()))
		{
			if (input.Branches != nullptr)
			{
				for (const Core::Branch* choice : BaseChoices(*input.Branches, tab.Target.Branch))
				{
					const bool isCurrent = tab.Target.Base == choice->FullName;

					ImGui::PushID(choice->FullName.c_str());

					if (ImGui::Selectable(choice->Name.c_str(), isCurrent) && !isCurrent)
					{
						ChangeBase(tab, input.Model, choice->FullName);
					}

					ImGui::PopID();
				}
			}

			ImGui::EndCombo();
		}

		if (range != nullptr && range->Problem == RangeProblem::None)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", RangeSummary(*range).c_str());

			if (!range->Commits.empty() && ImGui::BeginItemTooltip())
			{
				const std::size_t shown = std::min(range->Commits.size(), kCommitsInTooltip);

				for (std::size_t commitIndex = 0; commitIndex < shown; ++commitIndex)
				{
					const Core::CommitSummary& commit = range->Commits[commitIndex];
					ImGui::TextDisabled("%s", commit.Id.ShortHex().c_str());
					ImGui::SameLine();
					ImGui::TextUnformatted(commit.Summary.c_str());
				}

				if (range->Commits.size() > shown)
				{
					ImGui::TextDisabled("and %zu more", range->Commits.size() - shown);
				}

				ImGui::EndTooltip();
			}
		}

		ImGui::SameLine();

		const bool canInclude = range != nullptr && range->Worktree.has_value();
		bool include = tab.IncludeUncommitted && canInclude;

		ImGui::BeginDisabled(!canInclude);

		if (ImGui::Checkbox("Include uncommitted", &include))
		{
			SetIncludeUncommitted(tab, input.Model, include);
		}

		ImGui::EndDisabled();

		if (range != nullptr && !range->Worktree.has_value())
		{
			ImGui::SetItemTooltip("Not checked out in any worktree");
		}

		if (viewed.has_value())
		{
			const std::string viewedText = std::format("{}/{} viewed", viewed->Viewed, viewed->Total);

			ImGui::SameLine();
			const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - MeasureText(viewedText)));
			ImGui::TextDisabled("%s", viewedText.c_str());
		}
	}

	void ReviewWindows::RenderBody(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review)
	{
		RefreshRows(tab, review);

		const ReviewGroups groups = GroupsOf(tab, review);
		const OpenRelativeFile openFile = OpenFileIn(*review.Range);

		RenderHeader(tab, input, review.Range.get(), ViewedCountOf(tab, input.Sessions, groups));

		const bool alreadyMerged = review.Range->Ends.MergeBase == review.Range->Ends.Tip;

		if (alreadyMerged)
		{
			ImGui::TextDisabled("%s", AlreadyMergedMessage(tab.Target.Branch, *tab.Target.Base).c_str());
		}

		const Core::Error* uncommittedError =
			tab.IncludeUncommitted ? input.Model.GetUncommittedError(review.Key) : nullptr;

		if (uncommittedError != nullptr && review.Range->Worktree.has_value())
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", UncommittedProblem(*uncommittedError, *review.Range->Worktree).c_str());
			ImGui::SameLine();

			if (ImGui::SmallButton("Retry"))
			{
				input.Model.Retry(review.Key);
			}
		}

		const bool worktreeGone = tab.IncludeUncommitted && !review.Range->Worktree.has_value() && tab.LastWorktree.has_value();

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
			RenderFileList(tab, input, groups, openFile);

			ImGui::TableSetColumnIndex(1);
			RenderSelectedDiff(tab, input, review, groups, openFile);

			ImGui::EndTable();
		}
	}

	void ReviewWindows::RenderFileList(Tab& tab, const ReviewRenderInput& input, const ReviewGroups& groups,
									   const OpenRelativeFile& openFile)
	{
		// NoNavInputs: the arrow keys and Space are this list's own, below, rather than ImGui's
		// keyboard navigation, which would also toggle whichever checkbox it had reached.
		const ImVec2 fillTheCell{0.0f, 0.0f};

		if (ImGui::BeginChild("##reviewFiles", fillTheCell, ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs))
		{
			const std::vector<ReviewListRow>& rows = tab.Rows.Rows;
			const float rowHeight = ImGui::GetFrameHeight();
			const std::optional<std::size_t> selectedRow = RowOfSelection(tab.Selected, groups, rows);

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()), ImGui::GetFrameHeightWithSpacing());

			if (tab.ScrollToSelected && selectedRow.has_value())
			{
				clipper.IncludeItemByIndex(static_cast<int>(*selectedRow));
			}

			while (clipper.Step())
			{
				for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
				{
					const std::size_t rowPosition = static_cast<std::size_t>(rowIndex);
					const ReviewListRow& row = rows[rowPosition];

					ImGui::PushID(static_cast<int>(IndexOf(row.Group)));

					if (row.IsHeading)
					{
						bool& collapsed = tab.Collapsed[IndexOf(row.Group)];

						if (DrawGroupHeading(row.Group, row.FileCount, collapsed, rowHeight))
						{
							collapsed = !collapsed;
						}
					}
					else
					{
						const bool isSelected = selectedRow == rowPosition;

						RenderFileRow(tab, input, groups, row, tab.Rows.Counts[rowPosition], isSelected, openFile);

						if (isSelected && tab.ScrollToSelected)
						{
							if (!ImGui::IsItemVisible())
							{
								ImGui::SetScrollHereY();
							}

							tab.ScrollToSelected = false;
						}
					}

					ImGui::PopID();
				}
			}

			HandleListKeys(tab, input.Sessions, groups);
		}

		ImGui::EndChild();
	}

	void ReviewWindows::RenderFileRow(Tab& tab, const ReviewRenderInput& input, const ReviewGroups& groups,
									  const ReviewListRow& row, const LineCounts& counts, bool isSelected,
									  const OpenRelativeFile& openFile)
	{
		const Core::FileDiff& file = FileOfRow(groups, row);
		const std::filesystem::path& path = ShownPath(file);
		const bool fileIsOnDisk = file.Change != Core::FileChange::Deleted;

		ImGui::PushID(Core::ToUtf8(path).c_str());

		const FileRowLook look{.IsSelected = isSelected,
							   .Viewed = input.Sessions.StateOf(tab.Target.Branch, row.Group, file),
							   .CanOpen = openFile != nullptr && fileIsOnDisk,
							   .RowHeight = ImGui::GetFrameHeight(),
							   .Counts = counts};

		const FileRowActions actions = DrawFileRow(file, look, input.Palette);

		ImGui::PopID();

		if (actions.Selected)
		{
			tab.Selected = ReviewSelection{.Group = row.Group, .Path = path};
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

	void ReviewWindows::HandleListKeys(Tab& tab, ReviewSessions& sessions, const ReviewGroups& groups)
	{
		if (!ImGui::IsWindowFocused())
		{
			return;
		}

		const std::vector<ReviewListRow>& rows = tab.Rows.Rows;

		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
		{
			tab.Selected = NeighbourSelection(tab.Selected, groups, rows, ListStep::Next);
			tab.ScrollToSelected = true;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
		{
			tab.Selected = NeighbourSelection(tab.Selected, groups, rows, ListStep::Previous);
			tab.ScrollToSelected = true;
		}

		const bool noRepeat = false;

		if (!ImGui::IsKeyPressed(ImGuiKey_Space, noRepeat) || !tab.Selected.has_value())
		{
			return;
		}

		const Core::FileDiff* file = SelectedFile(groups, *tab.Selected);

		if (file != nullptr)
		{
			ToggleViewed(tab, sessions, tab.Selected->Group, *file);
		}
	}

	void ReviewWindows::RenderSelectedDiff(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
										   const ReviewGroups& groups, const OpenRelativeFile& openFile)
	{
		if (ImGui::BeginChild("##reviewDiff"))
		{
			const Core::FileDiff* file = tab.Selected.has_value() ? SelectedFile(groups, *tab.Selected) : nullptr;

			if (file == nullptr)
			{
				Core::EmptyState(HasFileRow(tab.Rows.Rows) ? "Select a file to see its changes." : "Nothing to review.");
			}
			else
			{
				UpdateTexts(tab, input.Model, review, *file);

				tab.View.OnOpenFile = openFile;
				tab.View.OnCopied = OnCopied;
				tab.View.OnSetWordWrap = OnSetWordWrap;
				tab.View.Render(*file, input.Palette, input.Syntax, input.Selection, tab.Texts, tab.Tokens,
								input.WordWrap);
			}
		}

		ImGui::EndChild();
	}
}

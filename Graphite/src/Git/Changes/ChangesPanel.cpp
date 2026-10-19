#include "Git/Changes/ChangesPanel.h"

#include "Core/Base/Base.h"

#include "Editor/DocumentWindows.h"
#include "Theme/Palettes.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"
#include "UI/ClipboardCopy.h"
#include "Editor/StatusBar/StatusMessageEvent.h"
#include "Git/Changes/CommitBlocker.h"
#include "Git/History/HistoryPlaceholder.h"
#include "Git/InitRepositoryPrompt.h"
#include "Theme/ChromePalette.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/FileIcons.h"
#include "UI/FittedText.h"
#include "UI/TickBox.h"
#include "UI/ToastEvent.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextInput.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		float RowPadding()
		{
			return ImGui::GetStyle().FramePadding.x * 2.0f;
		}

		constexpr bool kNotEngaged = false;
		constexpr Core::IconButtonOptions kRowButton{.Fit = Core::IconButtonFit::Line};

		float RowButtonWidth()
		{
			return Core::IconButtonSize(Core::IconButtonFit::Line);
		}

		float MessageBoxHeight()
		{
			constexpr float kMessageBoxLines = 2.0f;

			return ImGui::GetFrameHeight() * kMessageBoxLines;
		}

		float CommitBoxHeight()
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			// WARNING: Separator draws at least one pixel thick, whatever SeparatorSize says.
			const float separatorThickness = std::max(style.SeparatorSize, 1.0f);

			return separatorThickness + style.ItemSpacing.y + MessageBoxHeight() +
				   style.ItemSpacing.y + ImGui::GetFrameHeight();
		}

		// WARNING: Icon glyphs are not all equally wide, so this is the font size and not a
		// measured glyph: the column must have one width for files, folders and chevrons.
		float IconColumnWidth()
		{
			return ImGui::GetFontSize();
		}

		float TextGap()
		{
			constexpr float kInnerSpacingsPerGap = 2.0f;

			return ImGui::GetStyle().ItemInnerSpacing.x * kInnerSpacingsPerGap;
		}

		float TreeIndent()
		{
			return IconColumnWidth() + TextGap();
		}

		float FileRowIconInkToNameGap()
		{
			const Core::GlyphInk plainFileIcon = Core::MeasureGlyphInk(GlyphOf(FileKind::Other));
			return IconColumnWidth() - plainFileIcon.EndFromDrawX + TextGap();
		}

		float TypicalNameSideBearing()
		{
			constexpr std::string_view kTypicalLowercaseLetter = "n";

			return Core::MeasureGlyphInk(kTypicalLowercaseLetter).StartFromDrawX;
		}

		float MinDirectoryWidth()
		{
			constexpr float kMinimumDirectoryFontSizes = 3.0f;

			return ImGui::GetFontSize() * kMinimumDirectoryFontSizes;
		}

		ImVec4 TintedName(unsigned int statusColor)
		{
			constexpr float kTint = 0.45f;

			const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImVec4 status = ImGui::ColorConvertU32ToFloat4(statusColor);

			return ImVec4(text.x + (status.x - text.x) * kTint,
						  text.y + (status.y - text.y) * kTint,
						  text.z + (status.z - text.z) * kTint, text.w);
		}

		bool IsStagedAndNotConflicted(const Core::FileStatus& file)
		{
			return !file.IsConflicted && file.Index != Core::StatusFlags::None;
		}

		bool AnythingStaged(const Core::WorkingTreeStatus& status)
		{
			for (const Core::FileStatus& file : status.Entries)
			{
				if (IsStagedAndNotConflicted(file))
				{
					return true;
				}
			}

			return false;
		}
	}

	ChangesPanel::ChangesPanel(GitModel& model, ModalGate& modals,
							   const Core::DocumentSet& documents,
							   DocumentWindows& documentWindows, const ActiveScheme& scheme,
							   DiffWindows& diffWindows, Core::EventBus& events)
		: Core::Panel("changes", "Changes", "git", ICON_LC_FILE_DIFF), m_Model(model), m_Documents(documents),
		  m_DocumentWindows(documentWindows), m_Scheme(scheme), m_DiffWindows(diffWindows), m_Events(events),
		  m_DiscardQuestion(modals, "Discard changes?"), m_StashQuestion(modals, "Stash changes")
	{
	}

	// WARNING: PanelManager calls OnUpdate for every panel every frame but skips OnImGuiRender
	// for a hidden, collapsed or tabbed-behind panel. A question left open while the panel is not
	// drawn would hold the modal gate forever, so it is withdrawn here, queue and all: a discard
	// clicked while another modal held the gate is dropped, and the row is still there to click
	// again. TakeCommitted and TakeDiscarded are drained here for the same reason: their
	// expectation must be registered within milliseconds of the write that produced it.
	void ChangesPanel::OnUpdate(Core::Timestep)
	{
		if (!HasBeenDrawnOnLatestFrame() && m_DiscardQuestion.IsAsked())
		{
			m_DiscardQuestion.Withdraw();
		}

		if (!HasBeenDrawnOnLatestFrame() && m_StashQuestion.IsAsked())
		{
			m_StashQuestion.Withdraw();
			ResetStashDialog();
		}

		if (const std::optional<Core::Oid> committed = m_Model.TakeCommitted(); committed.has_value())
		{
			std::string statusMessage = "Committed " + committed->ShortHex();

			const std::string trimmedMessage = Core::Trimmed(m_Message);
			const std::string firstLineOfMessage =
				Core::Trimmed(std::string_view(trimmedMessage).substr(0, trimmedMessage.find('\n')));

			if (!firstLineOfMessage.empty())
			{
				statusMessage += ' ';
				statusMessage += firstLineOfMessage;
			}

			m_Events.Publish(StatusMessageEvent{std::move(statusMessage)});

			m_Message.clear();
			m_Amend = false;
		}

		if (std::vector<std::filesystem::path> discarded = m_Model.TakeDiscarded(); !discarded.empty())
		{
			if (m_Model.GetWorkingDirectory().empty())
			{
				return;
			}

			// WARNING: Status paths are relative to GetWorkingDirectory(), not GetRoot():
			// Repository::Open searches upward for a .git, so the two can differ. A discarded
			// directory is expanded into the files remembered in m_DiscardedFiles, because the
			// editor holds documents and the directory is already gone from disk.
			std::vector<std::filesystem::path> absolutePaths;
			absolutePaths.reserve(discarded.size());

			for (const std::filesystem::path& discardedPath : discarded)
			{
				using DiscardedFilesMap =
					std::map<std::filesystem::path, std::vector<std::filesystem::path>>;

				DiscardedFilesMap::node_type rememberedEntry = m_DiscardedFiles.extract(discardedPath);

				if (rememberedEntry.empty())
				{
					absolutePaths.push_back(m_Model.GetWorkingDirectory() / discardedPath);
					continue;
				}

				for (const std::filesystem::path& rememberedFile : rememberedEntry.mapped())
				{
					absolutePaths.push_back(m_Model.GetWorkingDirectory() / rememberedFile);
				}
			}

			m_DocumentWindows.ExpectExternalWrite(absolutePaths);
		}
	}

	void ChangesPanel::OnImGuiRender()
	{
		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		const Core::Ref<const Core::WorkingTreeStatus> status = m_Model.GetStatus();

		if (status == nullptr)
		{
			m_Selected.reset();

			for (std::vector<std::filesystem::path>& lastDrawn : m_LastDrawn)
			{
				lastDrawn.clear();
			}

			m_Expanded.clear();
			m_DiscardedFiles.clear();

			// WARNING: The questions are withdrawn before the return below: Begin is the only
			// other code that hands the modal gate back, and a workspace closed while a question
			// was on screen would otherwise hold it forever.
			m_DiscardQuestion.Withdraw();
			m_StashQuestion.Withdraw();
			ResetStashDialog();

			constexpr bool kHasStatus = false;
			const std::optional<Core::Error>& statusError = m_Model.GetStatusError();

			if (IsMissingRepository(kHasStatus, statusError))
			{
				DrawInitRepositoryPrompt(m_Model);
			}
			else if (statusError.has_value())
			{
				Core::EmptyState("Could not read the repository: " + statusError->Message);
			}
			else if (!m_Model.HasOpenFolder())
			{
				// WARNING: With no folder no status is on its way, so a loader here would turn forever.
				Core::EmptyState("No folder is open.");
			}
			else
			{
				Core::LoadingState("Loading changes");
			}

			return;
		}

		const ChangeSections sections = PartitionStatus(*status);

		const float reservedBoxHeight = CommitBoxHeight() + ImGui::GetStyle().ItemSpacing.y;

		if (ImGui::BeginChild("changes.lists", ImVec2(0.0f, -reservedBoxHeight)))
		{
			bool drawnAbove = false;

			m_Section = Section::Conflicted;
			drawnAbove = RenderSection("CONFLICTED", sections.Conflicted, *status, palette,
									   drawnAbove);

			m_Section = Section::Staged;
			drawnAbove = RenderSection("STAGED", sections.Staged, *status, palette, drawnAbove) ||
						 drawnAbove;

			m_Section = Section::Unstaged;
			const bool drawnUnstaged =
				RenderSection("CHANGES", sections.Unstaged, *status, palette, drawnAbove);

			if (!drawnAbove && !drawnUnstaged)
			{
				Core::EmptyState("No changes");
			}
		}

		ImGui::EndChild();

		RenderCommitBox();
		RenderDiscardConfirm();
		RenderStashDialog();
	}

	bool ChangesPanel::RenderSection(const char* heading, const std::vector<std::size_t>& rows,
									 const Core::WorkingTreeStatus& status,
									 const GitPalette& palette, bool spaced)
	{
		if (rows.empty())
		{
			m_LastDrawn[static_cast<std::size_t>(m_Section)].clear();
			return false;
		}

		ImGui::PushID(heading);

		if (spaced)
		{
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
		}

		const std::size_t sectionSlot = static_cast<std::size_t>(m_Section);

		const float rowHeight = ListRowLineHeight();

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const ImVec2 rowScreenStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		constexpr bool kNeverSelected = false;

		if (ListRowSelectable("##header", kNeverSelected, ImGuiSelectableFlags_AllowOverlap))
		{
			m_Collapsed[sectionSlot] = !m_Collapsed[sectionSlot];
		}

		const ImVec2 rowScreenEnd(rowScreenStart.x + rowWidth,
								  rowScreenStart.y + ImGui::GetItemRectSize().y);

		const bool rowHovered =
			ImGui::IsMouseHoveringRect(ImVec2(ListLeft(), ImGui::GetItemRectMin().y), ImVec2(rowScreenEnd.x, ImGui::GetItemRectMax().y));

		const bool open = !m_Collapsed[sectionSlot];

		const float rowPadding = RowPadding();
		const float textStart = rowStart.x + rowPadding + IconColumnWidth() + TextGap();

		ImGui::SetCursorPos(ImVec2(rowStart.x + rowPadding, ListRowTextTop(rowStart.y)));
		DrawChevron(open);

		ImGui::SetCursorPos(ImVec2(textStart, ListRowTextTop(rowStart.y)));
		ImGui::TextDisabled("%s", heading);
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu)", rows.size());

		if (rowHovered)
		{
			const char* allIcon = m_Section == Section::Staged ? ICON_LC_MINUS : ICON_LC_PLUS;

			ImGui::SetCursorScreenPos(ImVec2(ListRowEnd() - RowButtonWidth(), ListRowTextTop(rowScreenStart.y)));

			ImGui::BeginDisabled(m_Model.IsWriteInFlight());

			if (Core::DrawIconButton("##stageAll", allIcon, kNotEngaged,
									 m_Section == Section::Staged ? "Unstage all" : "Stage all", kRowButton))
			{
				std::vector<std::filesystem::path> paths;
				paths.reserve(rows.size());

				for (const std::size_t entryIndex : rows)
				{
					paths.push_back(status.Entries[entryIndex].Path);
				}

				if (m_Section == Section::Staged)
				{
					m_Model.RequestUnstage(std::move(paths));
				}
				else
				{
					m_Model.RequestStage(std::move(paths));
				}
			}

			ImGui::EndDisabled();
		}

		ImGui::SetCursorPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		std::vector<std::filesystem::path> drawn;
		drawn.reserve(rows.size());

		for (const std::size_t entryIndex : rows)
		{
			drawn.push_back(status.Entries[entryIndex].Path);
		}

		if (m_Selected && m_Selected->InSection == m_Section && drawn != m_LastDrawn[sectionSlot])
		{
			const std::optional<std::filesystem::path> movedSelection =
				NextSelection(m_Selected->Path, m_LastDrawn[sectionSlot], drawn);

			if (movedSelection.has_value())
			{
				m_Selected = Selection{.InSection = m_Section, .Path = *movedSelection};
			}
			else if (!SelectedIsInsideExpandedFolder())
			{
				m_Selected.reset();
			}
		}

		m_LastDrawn[sectionSlot] = drawn;

		if (open)
		{
			ImGui::Indent(TreeIndent());

			for (const std::size_t entryIndex : rows)
			{
				if (status.Entries[entryIndex].IsDirectory)
				{
					RenderFolderRow(entryIndex, status, palette);
				}
				else
				{
					RenderRow(entryIndex, status, palette);
				}
			}

			ImGui::Unindent(TreeIndent());
		}

		ImGui::PopID();

		return true;
	}

	void ChangesPanel::RenderRow(std::size_t entryIndex, const Core::WorkingTreeStatus& status,
								 const GitPalette& palette)
	{
		const Core::FileStatus& file = status.Entries[entryIndex];

		ImGui::PushID(static_cast<int>(entryIndex));

		const std::filesystem::path directory = file.Path.parent_path();

		const float rowHeight = ListRowLineHeight();

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const ImVec2 rowScreenStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		const Selection identity{.InSection = m_Section, .Path = file.Path};
		const bool selected = m_Selected.has_value() && *m_Selected == identity;

		Core::StatusFlags statusFlags = file.WorkTree;

		if (m_Section == Section::Conflicted)
		{
			statusFlags = file.Index | file.WorkTree;
		}
		else if (m_Section == Section::Staged)
		{
			statusFlags = file.Index;
		}

		const char letter = ChangeLetter(statusFlags, file.IsUntracked);
		const ImU32 letterColor = ColourOfLetter(letter, palette);

		// WARNING: The Selectable is an empty label over the whole row with AllowOverlap, so the
		// pieces and buttons laid over it still get their clicks. The buttons make it stop
		// counting as hovered, so hover is asked of the row's rectangle instead, read while the
		// Selectable is still the last item. The context menu must come directly after it for
		// the same reason.
		if (ListRowSelectable("##row", selected, ImGuiSelectableFlags_AllowOverlap))
		{
			m_Selected = identity;

			const OpenDiff diff{
				.Commit = std::nullopt, .Target = TargetForSection(), .Path = file.Path};

			// WARNING: GitModel keeps a single slot for the working-tree diff, so two tabs of
			// it would take turns failing. Closed only for a different diff: Open() re-docks an
			// exact match, and closing first would yank a tab the user moved back to the centre.
			if (!m_DiffWindows.IsOpen(diff))
			{
				m_DiffWindows.CloseWorkingTree();
			}

			m_DiffWindows.Open(diff);
		}

		const ImVec2 rowScreenEnd(rowScreenStart.x + rowWidth,
								  rowScreenStart.y + ImGui::GetItemRectSize().y);

		const bool rowHovered =
			ImGui::IsMouseHoveringRect(ImVec2(ListLeft(), ImGui::GetItemRectMin().y), ImVec2(rowScreenEnd.x, ImGui::GetItemRectMax().y));

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_Selected = identity;
		}

		RenderRowContextMenu(file, letter);

		const char* stageIcon = m_Section == Section::Staged ? ICON_LC_MINUS : ICON_LC_PLUS;

		const bool offersDiscard = m_Section == Section::Unstaged;

		const char* discardIcon = file.IsUntracked ? ICON_LC_TRASH_2 : ICON_LC_UNDO_2;

		const float discardWidth =
			offersDiscard ? RowButtonWidth() + ImGui::GetStyle().ItemSpacing.x : 0.0f;

		const float buttonsWidth = discardWidth + RowButtonWidth();

		const char letterText[] = {letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;

		const float rowPadding = RowPadding();
		const float textGap = TextGap();
		const float textStart = rowStart.x + rowPadding + IconColumnWidth() + textGap;
		const float rightEdge = rowStart.x + (ListRowEnd() - rowScreenStart.x);

		const float trailingWidth = rowHovered ? buttonsWidth : letterWidth;
		const float availableTextWidth =
			rightEdge - trailingWidth - ImGui::GetStyle().ItemSpacing.x - textStart;

		const std::string name = Core::ToUtf8(file.Path.filename());
		const std::string directoryText = Core::ToUtf8(directory);

		const RowText rowTextFit = FitRowText(
			availableTextWidth, ImGui::CalcTextSize(name.c_str()).x,
			directory.empty() ? 0.0f : ImGui::CalcTextSize(directoryText.c_str()).x, textGap,
			MinDirectoryWidth());

		ImGui::SetCursorPos(ImVec2(rowStart.x + rowPadding, ListRowTextTop(rowStart.y)));
		DrawFileIcon(KindOfFile(file.Path), ImGui::GetCursorScreenPos());

		ImGui::SetCursorPos(ImVec2(textStart, ListRowTextTop(rowStart.y)));
		ImGui::TextColored(TintedName(letterColor), "%s", FitToWidth(name, rowTextFit.Name).c_str());

		if (rowTextFit.Directory > 0.0f)
		{
			ImGui::SameLine(0.0f, textGap);
			ImGui::TextDisabled("%s", FitToWidth(directoryText, rowTextFit.Directory).c_str());
		}

		if (rowHovered)
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - buttonsWidth, ListRowTextTop(rowStart.y)));
			ImGui::BeginDisabled(m_Model.IsWriteInFlight());

			if (offersDiscard)
			{
				if (Core::DrawIconButton("##discard", discardIcon, kNotEngaged,
										 file.IsUntracked ? "Delete file" : "Discard changes", kRowButton))
				{
					BeginDiscard(file);
				}

				ImGui::SameLine();
			}

			if (Core::DrawIconButton("##stage", stageIcon, kNotEngaged,
									 m_Section == Section::Staged ? "Unstage" : "Stage", kRowButton))
			{
				if (m_Section == Section::Staged)
				{
					m_Model.RequestUnstage({file.Path});
				}
				else
				{
					m_Model.RequestStage({file.Path});
				}
			}

			ImGui::EndDisabled();
		}
		else
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - letterWidth, ListRowTextTop(rowStart.y)));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%s", letterText);
		}

		// WARNING: A window whose last operation is a SetCursorPos past its content aborts on an
		// assertion (ErrorCheckUsingSetCursorPosToExtendParentBoundaries). Every row laid out by
		// hand ends on a Dummy the size of the row, which leaves the cursor where the Selectable
		// would have.
		ImGui::SetCursorPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		ImGui::PopID();
	}

	void ChangesPanel::RenderFolderRow(std::size_t entryIndex, const Core::WorkingTreeStatus& status,
									   const GitPalette& palette)
	{
		const Core::FileStatus& file = status.Entries[entryIndex];

		ImGui::PushID(static_cast<int>(entryIndex));

		const float rowHeight = ListRowLineHeight();

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const ImVec2 rowScreenStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		const Selection identity{.InSection = m_Section, .Path = file.Path};
		const bool selected = m_Selected.has_value() && *m_Selected == identity;

		const Core::Ref<const Core::WorkingTreeStatus> listing =
			m_Model.GetUntrackedIn(file.Path);

		if (ListRowSelectable("##folder", selected, ImGuiSelectableFlags_AllowOverlap))
		{
			m_Selected = identity;

			if (m_Expanded.contains(file.Path))
			{
				m_Expanded.erase(file.Path);
			}
			else
			{
				m_Expanded.insert(file.Path);
			}
		}

		const ImVec2 rowScreenEnd(rowScreenStart.x + rowWidth,
								  rowScreenStart.y + ImGui::GetItemRectSize().y);

		const bool rowHovered =
			ImGui::IsMouseHoveringRect(ImVec2(ListLeft(), ImGui::GetItemRectMin().y), ImVec2(rowScreenEnd.x, ImGui::GetItemRectMax().y));

		const char letter = ChangeLetter(file.WorkTree, file.IsUntracked);
		const ImU32 letterColor = ColourOfLetter(letter, palette);

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_Selected = identity;
		}

		RenderRowContextMenu(file, letter);

		const bool expanded = m_Expanded.contains(file.Path);

		const char* chevron = ChevronGlyph(expanded);
		const char* folderIcon = FolderGlyphOf(expanded);

		const char* stageIcon = m_Section == Section::Staged ? ICON_LC_MINUS : ICON_LC_PLUS;

		const bool offersDiscard = m_Section == Section::Unstaged;

		const char* discardIcon = file.IsUntracked ? ICON_LC_TRASH_2 : ICON_LC_UNDO_2;

		const float discardWidth =
			offersDiscard ? RowButtonWidth() + ImGui::GetStyle().ItemSpacing.x : 0.0f;

		const float buttonsWidth = discardWidth + RowButtonWidth();

		const float textGap = TextGap();

		const char letterText[] = {letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;

		// WARNING: The count is shown only for an open folder git has answered for. The model
		// keeps a listing until the next status refresh even after the folder is closed, so a
		// count on a closed row would describe a listing nobody has re-asked for. The only other
		// number is one read off the disk, which is what git is asked instead of.
		const bool listingIsKnown = expanded && listing != nullptr;
		const std::string fileCountText =
			listingIsKnown ? std::format("({})", listing->Entries.size()) : std::string();

		const float fileCountWidth =
			fileCountText.empty() ? 0.0f : ImGui::CalcTextSize(fileCountText.c_str()).x + textGap;

		// WARNING: The trailing separator tells a directory from a file without an extension, so
		// it is held out of the elision and put back after it: a long name cut to fit must not
		// lose the one glyph carrying that.
		constexpr std::string_view kDirectorySeparator = "/";

		const float directorySeparatorWidth = ImGui::CalcTextSize(kDirectorySeparator.data()).x;

		constexpr bool kClosed = false;

		const FolderRowGlyphs closedGlyphs{.Folder = Core::MeasureGlyphInk(FolderGlyphOf(kClosed)),
										   .Chevron = Core::MeasureGlyphInk(ChevronGlyph(kClosed))};
		const FolderRowGlyphs drawnGlyphs{.Folder = Core::MeasureGlyphInk(folderIcon),
										  .Chevron = Core::MeasureGlyphInk(chevron)};

		const FolderRowPlacement placement = PlaceFolderRow(
			closedGlyphs, drawnGlyphs, FileRowIconInkToNameGap(), TypicalNameSideBearing());

		const float rowPadding = RowPadding();
		const float iconStart = rowStart.x + rowPadding;
		const float chevronStart = iconStart + placement.ChevronOffsetFromIcon;
		const float textStart = iconStart + placement.NameOffsetFromIcon;
		const float rightEdge = rowStart.x + (ListRowEnd() - rowScreenStart.x);

		const float trailingWidth = rowHovered ? buttonsWidth : letterWidth;

		const float nameLimit = rightEdge - trailingWidth - ImGui::GetStyle().ItemSpacing.x -
								fileCountWidth - directorySeparatorWidth - textStart;

		ImGui::SetCursorPos(ImVec2(iconStart, ListRowTextTop(rowStart.y)));
		DrawFolderIcon(expanded, ImGui::GetCursorScreenPos());

		ImGui::SetCursorPos(ImVec2(chevronStart, ListRowTextTop(rowStart.y)));
		DrawChevron(expanded);

		ImGui::SetCursorPos(ImVec2(textStart, ListRowTextTop(rowStart.y)));

		const std::string elidedName = FitToWidth(Core::ToUtf8(file.Path.filename()), nameLimit);

		const bool drawsName = !elidedName.empty();

		if (drawsName)
		{
			ImGui::TextColored(TintedName(letterColor), "%s",
							   (elidedName + std::string(kDirectorySeparator)).c_str());
		}

		if (drawsName && !fileCountText.empty())
		{
			ImGui::SameLine(0.0f, textGap);
			ImGui::TextDisabled("%s", fileCountText.c_str());
		}

		if (!rowHovered)
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - letterWidth, ListRowTextTop(rowStart.y)));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%s", letterText);
		}
		else
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - buttonsWidth, ListRowTextTop(rowStart.y)));
			ImGui::BeginDisabled(m_Model.IsWriteInFlight());

			if (offersDiscard)
			{
				if (Core::DrawIconButton("##discard", discardIcon, kNotEngaged,
										 file.IsUntracked ? "Delete folder" : "Discard changes", kRowButton))
				{
					BeginDiscard(file);
				}

				ImGui::SameLine();
			}

			if (Core::DrawIconButton("##stage", stageIcon, kNotEngaged,
									 m_Section == Section::Staged ? "Unstage" : "Stage", kRowButton))
			{
				if (m_Section == Section::Staged)
				{
					m_Model.RequestUnstage({file.Path});
				}
				else
				{
					m_Model.RequestStage({file.Path});
				}
			}

			ImGui::EndDisabled();
		}

		ImGui::SetCursorPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		ImGui::PopID();

		if (!expanded)
		{
			return;
		}

		m_Model.RequestUntrackedIn(file.Path);

		if (listing == nullptr)
		{
			return;
		}

		const float childIndent = FolderChildIndent(placement, drawnGlyphs.Chevron, IconColumnWidth());

		ImGui::Indent(childIndent);

		// WARNING: RenderRow identifies a row by its index into the status it was handed, so a
		// child at index 0 would share an ImGui id with the outer status's entry 0. The children
		// are scoped under the folder's own path.
		ImGui::PushID(Core::ToUtf8(file.Path).c_str());

		for (std::size_t childIndex = 0; childIndex < listing->Entries.size(); ++childIndex)
		{
			RenderRow(childIndex, *listing, palette);
		}

		ImGui::PopID();
		ImGui::Unindent(childIndent);
	}

	void ChangesPanel::RenderRowContextMenu(const Core::FileStatus& file, char letter)
	{
		const std::string popupId = "##ctx" + Core::ToUtf8(file.Path);

		if (!Core::BeginRightClickPopup(popupId.c_str()))
		{
			return;
		}

		const std::filesystem::path absolutePath = m_Model.GetWorkingDirectory() / file.Path;

		const bool existsOnDisk = letter != 'D';

		ImGui::BeginDisabled(!existsOnDisk);

		if (!file.IsDirectory)
		{
			constexpr bool kPermanent = true;

			if (ImGui::MenuItem("Open File"))
			{
				m_DocumentWindows.RequestOpen(absolutePath, kPermanent);
			}

			ImGui::Separator();
		}

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const Core::Result<void> revealed = Core::Shell::RevealInFileBrowser(absolutePath); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
				m_Events.Publish(FolderToast(Core::ToastKind::Error, "Cannot reveal file", revealed.GetError().Message,
											 m_Model.GetWorkingDirectory()));
			}
		}

		ImGui::EndDisabled();

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Path"))
		{
			CopyToClipboard(m_Events, "path", Core::ToUtf8(absolutePath));
		}

		if (ImGui::MenuItem("Copy Relative Path"))
		{
			CopyToClipboard(m_Events, "relative path", Core::ToUtf8(file.Path));
		}

		if (ImGui::MenuItem("Copy File Name"))
		{
			CopyToClipboard(m_Events, "file name", Core::ToUtf8(file.Path.filename()));
		}

		ImGui::EndPopup();
	}

	void ChangesPanel::RenderCommitBox()
	{
		const bool writeInFlight = m_Model.IsWriteInFlight();

		const float rowPadding = RowPadding();
		const float boxWidth = ImGui::GetContentRegionAvail().x - rowPadding * 2.0f;

		ImGui::Separator();

		ImGui::BeginDisabled(writeInFlight);

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rowPadding);

		const ImVec2 messageStart = ImGui::GetCursorScreenPos();

		Core::InputTextMultiline("##commitMessage", m_Message, ImVec2(boxWidth, MessageBoxHeight()));

		// WARNING: InputTextMultiline has no hint of its own, so an empty box is told what it is
		// for by hand, where the first character would go. With amend on, an empty box keeps the
		// message of the commit being replaced (CommitRequest::Amend).
		if (m_Message.empty())
		{
			const ImVec2 framePadding = ImGui::GetStyle().FramePadding;

			const char* hint = m_Amend ? "Keeping the message of the last commit" : "Message";

			ImGui::GetWindowDrawList()->AddText(
				ImVec2(messageStart.x + framePadding.x, messageStart.y + framePadding.y),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
		}

		ImGui::EndDisabled();

		const Core::Ref<const Core::WorkingTreeStatus> status = m_Model.GetStatus();
		const Core::Ref<const Core::HeadState> head = m_Model.GetHead();

		const std::optional<std::string_view> whyNot = WhyCommitIsDisabled(
			{.WriteInFlight = writeInFlight,
			 .AnythingStaged = status != nullptr && AnythingStaged(*status),
			 .Merging = head != nullptr && head->Operation == Core::RepositoryOperation::Merge,
			 .MessageIsEmpty = Core::Trimmed(m_Message).empty(),
			 .Amend = m_Amend});

		const bool canCommit = !whyNot.has_value();

		const float amendWidth = TickBoxWidth("Amend");

		const float stashWidth = ImGui::GetFrameHeight();

		// WARNING: ImGui reads a negative width as all but this much and zero as as wide as the
		// label, so a panel narrower than the three controls would turn the button full-width at
		// the moment there is least room for it. Held above zero.
		constexpr float kMinimumCommitWidth = 1.0f;
		const float commitWidth =
			std::max(boxWidth - amendWidth - stashWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f,
					 kMinimumCommitWidth);

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rowPadding);

		const ImVec4 accent = Core::ToImVec4(ChromePaletteFor(m_Scheme.Get()).Accent);

		if (DialogLayout::FilledButton(ICON_LC_CHECK "  Commit", accent, commitWidth, canCommit))
		{
			m_Model.RequestCommit({.Message = m_Message, .Amend = m_Amend});
		}

		if (whyNot.has_value() &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%.*s", static_cast<int>(whyNot->size()), whyNot->data());
		}

		ImGui::SameLine();

		const bool canStash = !writeInFlight && status && !status->Entries.empty();

		ImGui::BeginDisabled(!canStash);

		const float framePaddingY = ImGui::GetStyle().FramePadding.y;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePaddingY, framePaddingY));

		const bool stashPressed = ImGui::Button(ICON_LC_ARCHIVE "##stash", ImVec2(stashWidth, 0.0f));

		ImGui::PopStyleVar();

		if (stashPressed && !m_StashQuestion.IsAsked())
		{
			const StashQuestion stashQuestion;
			m_StashQuestion.Ask(stashQuestion);
		}

		ImGui::EndDisabled();

		ImGui::SetItemTooltip("Stash changes…");

		ImGui::SameLine();

		ImGui::BeginDisabled(writeInFlight);
		DrawTickBox("Amend", &m_Amend);
		ImGui::EndDisabled();
	}

	Core::DiffTarget ChangesPanel::TargetForSection() const
	{
		return m_Section == Section::Staged ? Core::DiffTarget::IndexToHead
											: Core::DiffTarget::WorkTreeToIndex;
	}

	unsigned int ChangesPanel::ColourOfLetter(char letter, const GitPalette& palette) const
	{
		if (letter == 'A' || letter == 'U')
		{
			return palette.GutterAdded;
		}

		if (letter == 'D')
		{
			return palette.GutterRemoved;
		}

		return palette.GutterModified;
	}

	void ChangesPanel::BeginDiscard(const Core::FileStatus& file)
	{
		DiscardSubject subject;
		subject.Path = file.Path;
		subject.IsDirectory = file.IsDirectory;
		subject.IsUntracked = file.IsUntracked;

		// WARNING: The listing is used only when the folder is open, the test RenderFolderRow
		// applies before printing a count. The model keeps a listing after the folder is closed,
		// and a count from it would put a number in the dialog that the row refuses to show.
		// Asking git here would answer after the question did, or never.
		if (file.IsDirectory && m_Expanded.contains(file.Path))
		{
			if (const Core::Ref<const Core::WorkingTreeStatus> listing =
					m_Model.GetUntrackedIn(file.Path))
			{
				std::vector<std::filesystem::path> files;
				files.reserve(listing->Entries.size());

				for (const Core::FileStatus& child : listing->Entries)
				{
					files.push_back(child.Path);
				}

				subject.Listing = std::move(files);
			}
		}

		DiscardQuestion question;
		question.Subject = file.Path;
		question.Plan = PlanDiscard(subject, m_Model.GetWorkingDirectory(), DirtyDocuments());
		question.Text = DiscardQuestionText(subject, question.Plan);

		// WARNING: Every discard asks, because git can hand back none of what one throws away.
		// Asked once per subject: a row waiting behind another modal stays clickable, and a
		// second click would queue a duplicate asking about a file already discarded.
		m_DiscardQuestion.AskOnce(std::move(question));
	}

	std::vector<std::filesystem::path> ChangesPanel::DirtyDocuments() const
	{
		std::vector<std::filesystem::path> dirty;

		for (std::size_t documentIndex = 0; documentIndex < m_Documents.GetCount(); ++documentIndex)
		{
			const Core::OpenItem& item = m_Documents.At(documentIndex);

			if (!item.Document || !item.Document->IsModified())
			{
				continue;
			}

			dirty.push_back(item.Path);
		}

		return dirty;
	}

	bool ChangesPanel::SelectedIsInsideExpandedFolder() const
	{
		if (!m_Selected.has_value())
		{
			return false;
		}

		std::filesystem::path ancestor = m_Selected->Path.parent_path();

		while (!ancestor.empty())
		{
			if (m_Expanded.contains(ancestor))
			{
				return true;
			}

			std::filesystem::path parent = ancestor.parent_path();

			// WARNING: A path that is its own parent, a root, would loop forever. Status paths
			// are relative and never reach one, but nothing here forces that.
			if (parent == ancestor)
			{
				break;
			}

			ancestor = std::move(parent);
		}

		return false;
	}

	void ChangesPanel::RenderDiscardConfirm()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const DiscardQuestion* asked = m_DiscardQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		const DiscardQuestion& question = *asked;

		DialogLayout::Text(question.Text);
		DialogLayout::Gap();
		DialogLayout::MutedText("This cannot be undone.");

		if (!question.Plan.Dirty.empty())
		{
			DialogLayout::Gap();
			DialogLayout::Text(
				std::format("{} open file(s) have unsaved edits that would be lost.", question.Plan.Dirty.size()));
		}

		DialogLayout::Gap();

		// WARNING: Copied out before Answer(), which drops the payload `question` refers to.
		const DiscardRequest request{.Restore = question.Plan.Restore,
									 .Delete = question.Plan.Delete};

		const std::filesystem::path subject = question.Subject;
		const std::vector<std::filesystem::path> files = question.Plan.Files;

		const DialogLayout::ButtonSpec absentButton{};
		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			absentButton, {.Label = "Discard", .Enabled = !m_Model.IsWriteInFlight()}, DangerColourOf(m_Scheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			if (!files.empty())
			{
				m_DiscardedFiles[subject] = files;
			}

			m_Model.RequestDiscard(request);
			m_DiscardQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DiscardQuestion.Answer();
		}

		m_DiscardQuestion.End();
	}

	void ChangesPanel::ResetStashDialog()
	{
		m_StashMessage.clear();
		m_StashIncludeUntracked = false;
		m_StashSaveFailed = false;
	}

	std::vector<std::filesystem::path> ChangesPanel::DirtyDocumentsInRepository() const
	{
		const std::filesystem::path& repositoryRoot = m_Model.GetWorkingDirectory();
		std::vector<std::filesystem::path> inside;

		for (std::filesystem::path& path : DirtyDocuments())
		{
			// WARNING: An empty root, with no repository open yet, matches nothing on purpose:
			// there is nothing a stash could take, so nothing is saved.
			if (Core::IsSameOrWithin(path, repositoryRoot))
			{
				inside.push_back(std::move(path));
			}
		}

		return inside;
	}

	void ChangesPanel::RenderStashDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const bool open = m_StashQuestion.Begin() != nullptr;
		DialogLayout::AfterBegin();

		if (!open)
		{
			// WARNING: Begin withdraws the question itself when ImGui closes the popup (Escape),
			// which reaches nothing here, so the edits are dropped on the frame it is noticed.
			if (!m_StashQuestion.IsAsked())
			{
				ResetStashDialog();
			}

			return;
		}

		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		Core::InputTextWithHint("##stash-message", "Message (optional)", m_StashMessage);

		DialogLayout::Gap();
		DrawTickBox("Include untracked files", &m_StashIncludeUntracked);
		DialogLayout::Gap();

		const ImVec4 danger = DangerColourOf(m_Scheme.Get());

		if (m_StashSaveFailed)
		{
			DialogLayout::ErrorText("Could not save every file; nothing was stashed.", danger);
			DialogLayout::Gap();
		}

		const DialogLayout::ButtonSpec stashButton{.Label = "Stash",
												   .Enabled = !m_Model.IsWriteInFlight()};
		const DialogLayout::ButtonSpec noDestructiveButton{};

		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(stashButton, noDestructiveButton, danger);

		if (choice == DialogLayout::Choice::Safe)
		{
			// WARNING: Documents are saved first so the stash holds the edits on screen. A file
			// that cannot be written stops everything: stashing the older copy would hide the
			// edit from the tree and the reader would think it was kept.
			if (m_DocumentWindows.SaveDocuments(DirtyDocumentsInRepository()))
			{
				m_Model.RequestStashSave(m_StashMessage, m_StashIncludeUntracked);
				m_StashQuestion.Answer();
				ResetStashDialog();
			}
			else
			{
				m_StashSaveFailed = true;
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_StashQuestion.Answer();
			ResetStashDialog();
		}

		m_StashQuestion.End();
	}
}

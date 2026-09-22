#include "Git/ChangesPanel.h"

#include "Editor/DocumentWindows.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		// imgui_stdlib is not part of this build, and every other InputText in this codebase
		// binds a fixed char buffer rather than a std::string. The commit message has no
		// natural cap the way a rename field does, so it is bound to m_Message directly
		// through the same growable-buffer callback imgui_stdlib itself uses.
		int ResizeMessageCallback(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
			{
				auto* message = static_cast<std::string*>(data->UserData);

				// Catches a future second call site binding this callback to the wrong
				// string - the same assertion imgui_stdlib itself carries here.
				IM_ASSERT(data->Buf == message->data());

				message->resize(static_cast<std::size_t>(data->BufTextLen));
				data->Buf = message->data();
			}

			return 0;
		}

		// The gap the panel's rows keep from either edge. Two frame paddings: one is what a
		// button already holds inside itself, and text set that close to the edge still reads
		// as touching it.
		float RowPadding()
		{
			return ImGui::GetStyle().FramePadding.x * 2.0f;
		}

		// A SmallButton's width - its label plus one frame padding either side, not a frame's
		// height. The rows and the section headers all right-align against this, and they must
		// measure it the same way or they disagree about where the right edge is.
		float ButtonWidth(const char* label)
		{
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		}

		// How tall the message box is: two lines of typing, which is a subject and the blank
		// line under it. Anything longer is written in the editor, not in a panel.
		float MessageBoxHeight()
		{
			return ImGui::GetFrameHeight() * 2.0f;
		}

		// Everything RenderCommitBox draws, top to bottom, with the spacing between. Shared
		// with OnImGuiRender, which reserves exactly this much at the bottom of the panel so
		// the box does not move as the lists above it grow.
		float CommitBoxHeight()
		{
			return ImGui::GetFrameHeightWithSpacing() + MessageBoxHeight() +
				   ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight();
		}

		// The single-character ellipsis (U+2026) as the three bytes its UTF-8 encoding takes.
		constexpr std::string_view Ellipsis = "â¦";

		// `text` cut down until it fits `width` pixels, ending in an ellipsis when anything
		// was dropped. Empty when not even the ellipsis fits, since half an ellipsis says
		// less than nothing.
		//
		// Measured in pixels rather than in columns the way CommitLabels does it: that list
		// is drawn in a monospace face and lines its rows up as a table, while these rows are
		// proportional text whose only constraint is the button laid over the row's right
		// edge. Cut on code point boundaries all the same - a path can carry multi-byte
		// characters, and a cut inside one leaves a byte no decoder can read.
		std::string Elided(std::string text, float width)
		{
			if (ImGui::CalcTextSize(text.c_str()).x <= width)
			{
				return text;
			}

			const float ellipsisWidth = ImGui::CalcTextSize(Ellipsis.data()).x;

			if (ellipsisWidth > width)
			{
				return {};
			}

			std::size_t length = text.size();

			while (length > 0)
			{
				--length;

				// A UTF-8 continuation byte has the top two bits `10`, and belongs to the
				// code point whose lead byte precedes it.
				while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u)
				{
					--length;
				}

				const char* begin = text.c_str();

				if (ImGui::CalcTextSize(begin, begin + length).x + ellipsisWidth <= width)
				{
					break;
				}
			}

			text.resize(length);
			text += Ellipsis;

			return text;
		}
	}

	ChangesPanel::ChangesPanel(GitModel& model, const Core::DocumentSet& documents,
							   DocumentWindows& documentWindows, const ActiveScheme& scheme,
							   DiffWindows& diffWindows)
		: Core::Panel("changes", "Changes", "git"), m_Model(model), m_Documents(documents),
		  m_DocumentWindows(documentWindows), m_Scheme(scheme), m_DiffWindows(diffWindows)
	{
	}

	void ChangesPanel::OnUpdate(Core::Timestep)
	{
		// Drained here, not in OnImGuiRender: Core::PanelManager calls OnUpdate on every
		// panel every frame regardless of visibility, dock state or collapse, but skips
		// OnImGuiRender for a panel that is hidden, collapsed, or behind another dock tab.
		// TakeDiscarded's registration is only safe while it stays close to the write that
		// produced it - ADR 0501 narrows the window to the milliseconds between the write
		// landing and the watcher noticing it - and a one-shot left undrained for however
		// long the panel happens to stay off-screen would register as a fresh expectation
		// far outside that window instead.
		if (m_Model.TakeCommitted().has_value())
		{
			m_Message.clear();
			m_Amend = false;
		}

		if (std::vector<std::filesystem::path> discarded = m_Model.TakeDiscarded(); !discarded.empty())
		{
			// A discard can only have been requested from a panel that already had a status,
			// and a status is only ever delivered together with a working directory - so a
			// non-empty `discarded` here already implies GetWorkingDirectory() is not empty.
			// Checked anyway, structurally rather than by that inference, because an empty
			// working directory would otherwise join into a path relative to nothing and
			// register an expectation for the wrong file.
			if (m_Model.GetWorkingDirectory().empty())
			{
				return;
			}

			// TakeDiscarded hands back paths relative to the repository's working directory,
			// but ExpectExternalWrite requires absolute ones - see ExpectedWrites::Expect.
			// Joined onto GetWorkingDirectory(), not GetRoot(): Core::Repository::Open
			// searches upward for a .git, so the folder the user opened and the directory
			// status paths are relative to are not always the same one.
			std::vector<std::filesystem::path> absolute;
			absolute.reserve(discarded.size());

			for (const std::filesystem::path& path : discarded)
			{
				absolute.push_back(m_Model.GetWorkingDirectory() / path);
			}

			m_DocumentWindows.ExpectExternalWrite(absolute);
		}
	}

	void ChangesPanel::OnImGuiRender()
	{
		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		const std::shared_ptr<const Core::WorkingTreeStatus> status = m_Model.GetStatus();

		if (status == nullptr)
		{
			// No status means no rows drawn anywhere, so a stale selection or a stale
			// last-drawn list would only ever be compared against the working tree of a
			// repository that may no longer even be the one open when drawing resumes.
			m_Selected.reset();

			for (std::vector<std::filesystem::path>& lastDrawn : m_LastDrawn)
			{
				lastDrawn.clear();
			}

			// Expansions go with them: the next repository's directories are not this one's,
			// and a path that happened to exist in both would open by itself.
			m_Expanded.clear();

			ImGui::TextUnformatted("No repository.");
			return;
		}

		const ChangeSections sections = PartitionStatus(*status);

		// The lists scroll together above a commit box pinned to the bottom. Reserved by
		// height rather than drawn after, because the box must not move as the lists grow.
		const float boxHeight = CommitBoxHeight() + ImGui::GetStyle().ItemSpacing.y;

		if (ImGui::BeginChild("changes.lists", ImVec2(0.0f, -boxHeight)))
		{
			// An empty section is not drawn at all: a repository with no conflicts must not
			// carry an empty "Conflicted" heading forever.
			m_Section = Section::Conflicted;
			RenderSection("Conflicted", sections.Conflicted, *status, palette);

			m_Section = Section::Staged;
			RenderSection("Staged", sections.Staged, *status, palette);

			m_Section = Section::Unstaged;
			RenderSection("Changes", sections.Unstaged, *status, palette);
		}

		ImGui::EndChild();

		RenderWriteError();
		RenderCommitBox();
		RenderDiscardConfirm();
	}

	void ChangesPanel::RenderSection(const char* heading, const std::vector<std::size_t>& rows,
									 const Core::WorkingTreeStatus& status,
									 const GitPalette& palette)
	{
		if (rows.empty())
		{
			// A section that draws nothing still drew something: an empty list. Recorded here
			// rather than left stale, or the next non-empty frame would move the selection
			// against a list from before this section emptied.
			m_LastDrawn[static_cast<std::size_t>(m_Section)].clear();
			return;
		}

		ImGui::PushID(heading);

		const ImVec2 headerStart = ImGui::GetCursorPos();
		const float availableWidth = ImGui::GetContentRegionAvail().x;

		const std::string label = std::format("{} ({})", heading, rows.size());

		// AllowOverlap: the "stage all"/"unstage all" button is laid over the same row via
		// SameLine below, the same technique the file rows and HitList's group headers use.
		const bool open = ImGui::CollapsingHeader(
			label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

		const char* allLabel = m_Section == Section::Staged ? ICON_MDI_MINUS "  Unstage all"
															: ICON_MDI_PLUS "  Stage all";

		const float buttonWidth = ButtonWidth(allLabel);

		ImGui::SameLine(headerStart.x + availableWidth - buttonWidth - RowPadding());
		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		if (ImGui::SmallButton(allLabel))
		{
			// One write of many paths is one index write, rather than one per row.
			std::vector<std::filesystem::path> paths;
			paths.reserve(rows.size());

			for (const std::size_t entry : rows)
			{
				paths.push_back(status.Entries[entry].Path);
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

		std::vector<std::filesystem::path> drawn;
		drawn.reserve(rows.size());

		for (const std::size_t entry : rows)
		{
			drawn.push_back(status.Entries[entry].Path);
		}

		const std::size_t slot = static_cast<std::size_t>(m_Section);

		if (m_Selected && m_Selected->InSection == m_Section && drawn != m_LastDrawn[slot])
		{
			const auto moved = NextSelection(m_Selected->Path, m_LastDrawn[slot], drawn);

			if (moved.has_value())
			{
				m_Selected = Selection{.InSection = m_Section, .Path = *moved};
			}
			else
			{
				m_Selected.reset();
			}
		}

		m_LastDrawn[slot] = drawn;

		if (open)
		{
			for (const std::size_t entry : rows)
			{
				if (status.Entries[entry].IsDirectory)
				{
					RenderFolderRow(entry, status, palette);
				}
				else
				{
					RenderRow(entry, status, palette);
				}
			}
		}

		ImGui::PopID();
	}

	void ChangesPanel::RenderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
								 const GitPalette& palette)
	{
		const Core::FileStatus& file = status.Entries[entry];

		ImGui::PushID(static_cast<int>(entry));

		// Two lines, not one: the name and the buttons need the full width between them, and
		// a directory squeezed in beside them either collides with the buttons or is cut down
		// to nothing. A file at the repository root has no second line and gets a short row -
		// there is no directory to show, and a blank line saying so would be worse.
		const std::filesystem::path directory = file.Path.parent_path();

		const float lineHeight = ImGui::GetTextLineHeight();
		const float lineGap = ImGui::GetStyle().ItemInnerSpacing.y;
		const float rowHeight =
			directory.empty() ? lineHeight : lineHeight * 2.0f + lineGap;

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const ImVec2 rowScreenStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		const Selection identity{.InSection = m_Section, .Path = file.Path};
		const bool selected = m_Selected.has_value() && *m_Selected == identity;

		// A conflicted entry is not split into a staged and an unstaged half the way an
		// ordinary file is - see ChangesRows.h - so its letter is read off both sides
		// together rather than off whichever one PartitionStatus happened to file it under.
		Core::StatusFlags flags = file.WorkTree;

		if (m_Section == Section::Conflicted)
		{
			flags = file.Index | file.WorkTree;
		}
		else if (m_Section == Section::Staged)
		{
			flags = file.Index;
		}

		// Worked out before the row is drawn rather than where it is first painted, because the
		// context menu below needs it too and a menu attached to the wrong item is worse than a
		// few lines of distance between a value and its use.
		const char letter = ChangeLetter(flags, file.IsUntracked);
		const ImU32 letterColor = ColourOfLetter(letter, palette);

		// An empty label over the whole row: the letter, name and directory are laid over it
		// in pieces below, and the buttons are laid over its right edge. AllowOverlap keeps
		// every click reaching whichever of those actually sits under the mouse.
		if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
							  ImVec2(0.0f, rowHeight)))
		{
			m_Selected = identity;

			const OpenDiff diff{
				.Commit = std::nullopt, .Target = TargetForSection(), .Path = file.Path};

			// Exactly one working-tree tab at a time: GitModel keeps a single slot for this
			// diff, so a second tab would intermittently show "that file is not in this
			// diff" as the two took turns asking into it. Only closed when this click asks
			// for a *different* diff than the one already open - Open() already re-docks and
			// re-raises an exact match, and closing first would throw that away, yanking a
			// tab the user dragged elsewhere back to the centre node on every click.
			if (!m_DiffWindows.IsOpen(diff))
			{
				m_DiffWindows.CloseWorkingTree();
			}

			m_DiffWindows.Open(diff);
		}

		// The buttons are laid over this row's right edge, so the moment the mouse reaches one
		// of them the Selectable underneath stops counting as hovered. Asked of the rectangle
		// instead, which stays true across the whole row and keeps a button from vanishing out
		// from under the hand reaching for it.
		//
		// Read here, while the Selectable is still the last item: everything below lays items
		// of its own over the row.
		const ImVec2 rowScreenEnd(rowScreenStart.x + rowWidth,
								  rowScreenStart.y + ImGui::GetItemRectSize().y);

		const bool rowHovered = ImGui::IsMouseHoveringRect(rowScreenStart, rowScreenEnd);

		// Selected first, so the highlight and the menu about to open talk about the same file.
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_Selected = identity;
		}

		// Still directly after the Selectable, which is the item the menu has to attach to -
		// the reads above take no item of their own.
		RenderRowContextMenu(file, letter);

		const char* stageIcon = m_Section == Section::Staged ? ICON_MDI_MINUS : ICON_MDI_PLUS;

		// Discard is offered only where it has something to throw away: a staged or conflicted
		// row's working-tree half is not what this button would act on.
		const bool offersDiscard = m_Section == Section::Unstaged;

		// An untracked file has nothing in the index to be restored from, so discarding it can
		// only mean deleting it - a different outcome, and one the icon says out loud.
		const char* discardIcon = file.IsUntracked ? ICON_MDI_DELETE : ICON_MDI_UNDO;

		const float discardWidth =
			offersDiscard ? ButtonWidth(discardIcon) + ImGui::GetStyle().ItemSpacing.x : 0.0f;

		const float buttonsWidth = discardWidth + ButtonWidth(stageIcon);

		// One inner spacing is the gap between an item and its own label; the letter and the
		// name are separate readings of one file, so they are given twice that to sit apart by.
		const float gap = ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;

		const char letterText[] = {letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;

		// Where the second line begins: under the name, not under the letter, so the letter
		// column stays a column and the two lines read as one entry.
		const float textStart = rowStart.x + RowPadding() + letterWidth + gap;

		// The name is cut short of the buttons rather than run underneath them. Their space is
		// held even while they are hidden, so this budget does not change as the mouse arrives.
		const float nameLimit = rowStart.x + rowWidth - RowPadding() - buttonsWidth -
								ImGui::GetStyle().ItemSpacing.x - textStart;

		ImGui::SetCursorPos(ImVec2(rowStart.x + RowPadding(), rowStart.y));
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%s", letterText);
		ImGui::SameLine(0.0f, gap);
		ImGui::TextUnformatted(Elided(Core::ToUtf8(file.Path.filename()), nameLimit).c_str());

		ImGui::SameLine(rowStart.x + rowWidth - RowPadding() - buttonsWidth);

		if (rowHovered)
		{
			ImGui::BeginDisabled(m_Model.IsWriteInFlight());

			if (offersDiscard)
			{
				if (ImGui::SmallButton(discardIcon))
				{
					BeginDiscard(file);
				}

				ImGui::SetItemTooltip("%s", file.IsUntracked ? "Delete file" : "Discard changes");
				ImGui::SameLine();
			}

			if (ImGui::SmallButton(stageIcon))
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

			ImGui::SetItemTooltip("%s", m_Section == Section::Staged ? "Unstage" : "Stage");

			ImGui::EndDisabled();
		}

		if (!directory.empty())
		{
			// The full width of the row is the second line's to use: nothing is laid over it.
			const float directoryLimit = rowWidth - RowPadding() - (textStart - rowStart.x);

			ImGui::SetCursorPos(ImVec2(textStart, rowStart.y + lineHeight + lineGap));
			ImGui::TextDisabled("%s", Elided(Core::ToUtf8(directory), directoryLimit).c_str());
		}

		// The row is drawn by moving the cursor around, and a window whose last operation is a
		// SetCursorPos past its own content aborts on an assertion - see
		// ErrorCheckUsingSetCursorPosToExtendParentBoundaries. Ending on an item clears that,
		// and a Dummy the size of the row leaves the cursor exactly where the Selectable would
		// have: one row height plus one item spacing below where this one began.
		ImGui::SetCursorPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		ImGui::PopID();
	}

	void ChangesPanel::RenderFolderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
									   const GitPalette& palette)
	{
		const Core::FileStatus& file = status.Entries[entry];

		ImGui::PushID(static_cast<int>(entry));

		// One line, always. The second line a file row spends on its parent directory would
		// here repeat what the name already says for a directory at the root, and a nested
		// one is reached by opening its parent rather than by reading a path off the row.
		const float rowHeight = ImGui::GetTextLineHeight();

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const ImVec2 rowScreenStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		const Selection identity{.InSection = m_Section, .Path = file.Path};
		const bool selected = m_Selected.has_value() && *m_Selected == identity;

		// The listing is read before the row is laid out, not after: the file count is part
		// of the row's width budget, and a count measured after the name was cut would let
		// the two disagree about where the name may end.
		const std::shared_ptr<const Core::WorkingTreeStatus> listing =
			m_Model.GetUntrackedIn(file.Path);

		// An empty label over the whole row, the same as a file row - see RenderRow. A
		// directory has no diff, so the click opens and closes it instead.
		if (ImGui::Selectable("##folder", selected, ImGuiSelectableFlags_AllowOverlap,
							  ImVec2(0.0f, rowHeight)))
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

		// Read off the rectangle rather than the Selectable, for the reason RenderRow gives:
		// the buttons laid over the row's right edge would otherwise take the hover away from
		// the row that decides whether they are drawn at all.
		const ImVec2 rowScreenEnd(rowScreenStart.x + rowWidth,
								  rowScreenStart.y + ImGui::GetItemRectSize().y);

		const bool rowHovered = ImGui::IsMouseHoveringRect(rowScreenStart, rowScreenEnd);

		const char letter = ChangeLetter(file.WorkTree, file.IsUntracked);
		const ImU32 letterColor = ColourOfLetter(letter, palette);

		// Selected first, for the reason RenderRow gives.
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_Selected = identity;
		}

		// Directly after the Selectable, as in RenderRow. The menu leaves out "Open File" of
		// its own accord, from file.IsDirectory.
		RenderRowContextMenu(file, letter);

		const bool expanded = m_Expanded.contains(file.Path);

		const char* chevron = expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;

		const char* stageIcon = m_Section == Section::Staged ? ICON_MDI_MINUS : ICON_MDI_PLUS;

		// The same rule a file row follows: discard only acts on the working tree, so it is
		// offered only where the working tree is what the row is showing.
		const bool offersDiscard = m_Section == Section::Unstaged;

		// Nothing in the index to restore a wholly untracked directory from, so this deletes.
		const char* discardIcon = file.IsUntracked ? ICON_MDI_DELETE : ICON_MDI_UNDO;

		const float discardWidth =
			offersDiscard ? ButtonWidth(discardIcon) + ImGui::GetStyle().ItemSpacing.x : 0.0f;

		const float buttonsWidth = discardWidth + ButtonWidth(stageIcon);

		const float innerGap = ImGui::GetStyle().ItemInnerSpacing.x;
		const float gap = innerGap * 2.0f;

		const char letterText[] = {letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;
		const float chevronWidth = ImGui::CalcTextSize(chevron).x;

		// Only while the folder is open and git has answered for it. One still being counted
		// and one whose count failed both say nothing rather than a number, because the only
		// other number available is one read off the disk - which is the very thing git is
		// being asked instead of. A closed folder says nothing either: the model holds its
		// listing until the next status refresh drops it, so a count left on a closed row
		// would go on describing a listing nobody has re-asked for.
		const std::string count = expanded && listing != nullptr
									  ? std::format("({})", listing->Entries.size())
									  : std::string();

		// Derived from the string that will actually be drawn, so the name's budget below can
		// never reserve room for a count this row has decided not to show.
		const float countWidth =
			count.empty() ? 0.0f : ImGui::CalcTextSize(count.c_str()).x + gap;

		// The trailing separator is what tells a reader at a glance that this row is a
		// directory and not a file that happens to have no extension. Held out of the
		// elision and put back after it: a long name cut to fit would otherwise lose the one
		// glyph carrying that, in exactly the case where the name is too long to recognise
		// on its own. Its width comes off the budget so the two together still fit.
		constexpr std::string_view Separator = "/";

		const float separatorWidth = ImGui::CalcTextSize(Separator.data()).x;

		const float textStart = rowStart.x + RowPadding() + chevronWidth + innerGap +
								letterWidth + gap;

		// Cut short of the buttons and of the count, the same way a file row is cut short of
		// the buttons alone.
		const float nameLimit = rowStart.x + rowWidth - RowPadding() - buttonsWidth -
								ImGui::GetStyle().ItemSpacing.x - countWidth - separatorWidth -
								textStart;

		ImGui::SetCursorPos(ImVec2(rowStart.x + RowPadding(), rowStart.y));
		ImGui::TextUnformatted(chevron);
		ImGui::SameLine(0.0f, innerGap);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%s", letterText);
		ImGui::SameLine(0.0f, gap);

		const std::string name =
			Elided(Core::ToUtf8(file.Path.filename()), nameLimit) + std::string(Separator);

		ImGui::TextUnformatted(name.c_str());

		if (!count.empty())
		{
			ImGui::SameLine(0.0f, gap);
			ImGui::TextDisabled("%s", count.c_str());
		}

		ImGui::SameLine(rowStart.x + rowWidth - RowPadding() - buttonsWidth);

		if (rowHovered)
		{
			ImGui::BeginDisabled(m_Model.IsWriteInFlight());

			if (offersDiscard)
			{
				if (ImGui::SmallButton(discardIcon))
				{
					BeginDiscard(file);
				}

				ImGui::SetItemTooltip("%s",
									  file.IsUntracked ? "Delete folder" : "Discard changes");
				ImGui::SameLine();
			}

			if (ImGui::SmallButton(stageIcon))
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

			ImGui::SetItemTooltip("%s", m_Section == Section::Staged ? "Unstage" : "Stage");

			ImGui::EndDisabled();
		}

		// Closed here rather than after the children - see the end of RenderRow for why a row
		// laid out by hand must end on an item. It also puts the cursor one row below this
		// one, which is where the first child has to begin.
		ImGui::SetCursorPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		ImGui::PopID();

		if (!expanded)
		{
			return;
		}

		// Asked on every frame the folder is open. The model answers a repeat ask for a
		// listing it already holds, already has in flight, or has already failed, with
		// nothing - see RequestUntrackedIn.
		m_Model.RequestUntrackedIn(file.Path);

		if (listing == nullptr)
		{
			return;
		}

		ImGui::Indent();

		// RenderRow identifies a row by its index into the status it was handed, so a child
		// at index 0 and the outer status's entry 0 would be the same ImGui id. Scoped under
		// the folder's own path, which no other row in this panel draws under.
		ImGui::PushID(Core::ToUtf8(file.Path).c_str());

		for (std::size_t child = 0; child < listing->Entries.size(); ++child)
		{
			RenderRow(child, *listing, palette);
		}

		ImGui::PopID();
		ImGui::Unindent();
	}

	void ChangesPanel::RenderRowContextMenu(const Core::FileStatus& file, char letter)
	{
		// The path is part of the id so that two rows of one section cannot share a menu. It is
		// enough on its own here: a folder's children are already drawn under a PushID of the
		// folder's path - see the end of RenderFolderRow - so a child and a top-level row of
		// the same name hash differently even when their paths read alike.
		const std::string popupId = "##ctx" + Core::ToUtf8(file.Path);

		if (!Core::BeginRightClickPopup(popupId.c_str()))
		{
			return;
		}

		// Joined onto the working directory and not the workspace root: Repository::Open
		// searches upward for a .git, so the two are not always the same directory and only
		// this one is what a status path is relative to.
		const std::filesystem::path absolute = m_Model.GetWorkingDirectory() / file.Path;

		// A deleted file is still a row, but there is nothing left on disk for either of these
		// to reach.
		const bool onDisk = letter != 'D';

		ImGui::BeginDisabled(!onDisk);

		if (!file.IsDirectory)
		{
			// Permanent, because choosing this from a menu is a deliberate open and not the
			// preview that the next click in a list is free to replace.
			if (ImGui::MenuItem("Open File"))
			{
				m_DocumentWindows.RequestOpen(absolute, true);
			}

			ImGui::Separator();
		}

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const auto revealed = Core::Shell::RevealInFileBrowser(absolute); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
			}
		}

		ImGui::EndDisabled();

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Path"))
		{
			ImGui::SetClipboardText(Core::ToUtf8(absolute).c_str());
		}

		// The path as the status names it, which is already relative to the working directory
		// - so unlike the explorer's, this needs no relative() call and no fallback for two
		// paths that share no prefix.
		if (ImGui::MenuItem("Copy Relative Path"))
		{
			ImGui::SetClipboardText(Core::ToUtf8(file.Path).c_str());
		}

		ImGui::EndPopup();
	}

	void ChangesPanel::RenderCommitBox()
	{
		const bool inFlight = m_Model.IsWriteInFlight();

		// The same inset the rows keep, so the box lines up with the list above it rather
		// than running out to edges nothing else touches.
		const float pad = RowPadding();
		const float width = ImGui::GetContentRegionAvail().x - pad * 2.0f;

		ImGui::BeginDisabled(inFlight);

		// Above the message and right-aligned: amend changes what the button at the bottom
		// will do, so it belongs to that pair rather than to the lists overhead.
		const float amendWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
								 ImGui::CalcTextSize("Amend").x;

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad + width - amendWidth);
		ImGui::Checkbox("Amend", &m_Amend);

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);

		const ImVec2 messageStart = ImGui::GetCursorScreenPos();

		ImGui::InputTextMultiline("##commitMessage", m_Message.data(), m_Message.size() + 1,
								  ImVec2(width, MessageBoxHeight()),
								  ImGuiInputTextFlags_CallbackResize, ResizeMessageCallback,
								  &m_Message);

		// InputTextMultiline has no hint of its own - InputTextWithHint is single-line only -
		// so an empty box is told what it is for by hand, drawn where the first character
		// would go so the hint and the typing that replaces it start at the same point.
		if (m_Message.empty())
		{
			const ImVec2 padding = ImGui::GetStyle().FramePadding;

			// An empty box means two different things depending on the checkbox above it, and
			// the difference is worth more than a fixed word: with amend on, leaving it empty
			// keeps the message of the commit being replaced - see CommitRequest::Amend.
			const char* hint = m_Amend ? "Keeping the message of the last commit" : "Message";

			ImGui::GetWindowDrawList()->AddText(
				ImVec2(messageStart.x + padding.x, messageStart.y + padding.y),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
		}

		ImGui::EndDisabled();

		// Whitespace-only counts as empty here - git would reject it the same way - but the
		// message sent to RequestCommit below is never trimmed; only this enablement test is.
		const bool canCommit = !inFlight && (!Core::Trimmed(m_Message).empty() || m_Amend);

		ImGui::BeginDisabled(!canCommit);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);

		// The full width of the box: this is the one thing the panel exists to let you do,
		// and a button that size cannot be missed or mistaken for one of the row controls.
		if (ImGui::Button(ICON_MDI_CHECK "  Commit", ImVec2(width, 0.0f)))
		{
			m_Model.RequestCommit({.Message = m_Message, .Amend = m_Amend});
		}

		ImGui::EndDisabled();
	}

	void ChangesPanel::RenderWriteError()
	{
		const std::optional<Core::Error>& error = m_Model.GetWriteError();

		if (!error.has_value())
		{
			return;
		}

		// A line, not a modal: a failed write is worth saying and not worth interrupting for
		// - the same choice GitView makes for a failed refresh.
		ImGui::TextUnformatted(error->Message.c_str());
		ImGui::SameLine();

		if (ImGui::SmallButton("Dismiss"))
		{
			m_Model.DismissWriteError();
		}
	}

	Core::DiffTarget ChangesPanel::TargetForSection() const
	{
		return m_Section == Section::Staged ? Core::DiffTarget::IndexToHead
											: Core::DiffTarget::WorkTreeToIndex;
	}

	unsigned int ChangesPanel::ColourOfLetter(char letter, const GitPalette& palette) const
	{
		if (letter == 'A' || letter == '?')
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
		m_PendingDiscard = {};

		// An untracked file has nothing in the index to be restored from, so discarding it can
		// only mean deleting it - a different outcome, and one the row's icon already said.
		if (file.IsUntracked)
		{
			m_PendingDiscard.Delete.push_back(file.Path);
		}
		else
		{
			m_PendingDiscard.Restore.push_back(file.Path);
		}

		std::vector<std::filesystem::path> every = m_PendingDiscard.Restore;
		every.insert(every.end(), m_PendingDiscard.Delete.begin(), m_PendingDiscard.Delete.end());

		m_PendingDirty = DirtyAmong(every);

		// Nothing to confirm in the ordinary case: what a discard throws away is what git
		// already holds a copy of, and a question whose answer is always yes is only a
		// second click. The exception is what git never saw - an open document with unsaved
		// edits - so that, and only that, is what the dialog below still asks about.
		if (m_PendingDirty.empty())
		{
			m_Model.RequestDiscard(m_PendingDiscard);
			return;
		}

		m_AskingAboutDiscard = true;
	}

	std::vector<std::filesystem::path> ChangesPanel::DirtyAmong(
		const std::vector<std::filesystem::path>& paths) const
	{
		std::vector<std::filesystem::path> dirty;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (!item.Document || !item.Document->IsModified())
			{
				continue;
			}

			const std::filesystem::path normalizedItem = Core::NormalizePath(item.Path);

			for (const std::filesystem::path& path : paths)
			{
				// `path` is git-relative; a document's path is absolute. Absolutised against
				// the repository's working directory - not the folder the user opened, which
				// Core::Repository::Open may have resolved upward from - before comparing whole
				// normalised paths. The same rule ExpectedWrites uses, and not the
				// filename-and-ends_with shortcut that was already withdrawn from ExpectedWrites
				// for making a root-level file match anywhere in the tree while never matching a
				// nested one at all.
				const std::filesystem::path normalizedPath =
					Core::NormalizePath(m_Model.GetWorkingDirectory() / path);

				if (Core::SamePath(normalizedItem, normalizedPath))
				{
					dirty.push_back(item.Path);
					break;
				}
			}
		}

		return dirty;
	}

	void ChangesPanel::RenderDiscardConfirm()
	{
		if (m_AskingAboutDiscard)
		{
			ImGui::OpenPopup("Discard changes##changesPanelDiscard");
			m_AskingAboutDiscard = false;
		}

		if (!ImGui::BeginPopupModal("Discard changes##changesPanelDiscard", nullptr,
									ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}

		// The only reason this dialog ever opens - see BeginDiscard - so it is the whole
		// question, not one warning among several.
		ImGui::TextUnformatted("Open with unsaved changes - those changes will be lost:");

		for (const std::filesystem::path& path : m_PendingDirty)
		{
			// The full path, not just the filename: two same-named files open from different
			// directories would otherwise make this warning ambiguous exactly where precision
			// matters.
			ImGui::BulletText("%s", Core::ToUtf8(path).c_str());
		}

		ImGui::Spacing();

		// The same guard every other write control in this panel takes: harmless today, since
		// the popup closes on press and the model serialises writes, but a reader should not
		// have to notice that to trust it.
		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		if (ImGui::Button("Discard"))
		{
			m_Model.RequestDiscard(m_PendingDiscard);
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

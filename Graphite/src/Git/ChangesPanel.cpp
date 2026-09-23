#include "Git/ChangesPanel.h"

#include "Editor/DocumentWindows.h"
#include "Theme/ChromePalette.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/Icons.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
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

		// Everything RenderCommitBox draws, top to bottom, with the spacing between: the
		// separator, the message box, and the row holding the button and the amend checkbox.
		// Shared with OnImGuiRender, which reserves exactly this much at the bottom of the
		// panel so the box does not move as the lists above it grow.
		float CommitBoxHeight()
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			// The thickness Separator itself uses, clamp and all.
			const float separator = std::max(style.SeparatorSize, 1.0f);

			return separator + style.ItemSpacing.y + MessageBoxHeight() + style.ItemSpacing.y +
				   ImGui::GetFrameHeight();
		}

		// The column every row starts with: a file's icon, a folder's chevron, a section's
		// chevron. One width for all three, so whatever follows them - a name, a heading -
		// starts at the same x on every row. The font size rather than a glyph's measured
		// width, because icon glyphs are not all equally wide and the column must not be.
		float IconColumnWidth()
		{
			return ImGui::GetFontSize();
		}

		// The gap between the icon column and the text, and between a name and whatever
		// follows it on the same line. One inner spacing is the gap between an item and its
		// own label; these are separate readings of one row, so they sit twice that apart.
		float TextGap()
		{
			return ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
		}

		// One level of nesting: rows under a heading, files under an open folder. Exactly the
		// icon column and its gap, so a nested row's icon lands under its parent's text and
		// every level lines up with the one above it.
		float TreeIndent()
		{
			return IconColumnWidth() + TextGap();
		}

		// The narrowest a directory is drawn at before it is dropped - see FitRowText.
		float MinDirectoryWidth()
		{
			return ImGui::GetFontSize() * 3.0f;
		}

		// A file's name in its status colour, pulled most of the way back towards the text
		// colour: tinted enough that a list can be read by colour without looking at the
		// letters, not so much that a name stops reading as plain text.
		ImVec4 TintedName(unsigned int statusColor)
		{
			constexpr float kTint = 0.45f;

			const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImVec4 status = ImGui::ColorConvertU32ToFloat4(statusColor);

			return ImVec4(text.x + (status.x - text.x) * kTint,
						  text.y + (status.y - text.y) * kTint,
						  text.z + (status.z - text.z) * kTint, text.w);
		}

		ImVec4 Scaled(const ImVec4& colour, float factor)
		{
			return ImVec4(std::min(colour.x * factor, 1.0f), std::min(colour.y * factor, 1.0f),
						  std::min(colour.z * factor, 1.0f), colour.w);
		}

		// The single-character ellipsis (U+2026) as the three bytes its UTF-8 encoding takes.
		constexpr std::string_view Ellipsis = "…";

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

	ChangesPanel::ChangesPanel(GitModel& model, ModalGate& modals,
							   const Core::DocumentSet& documents,
							   DocumentWindows& documentWindows, const ActiveScheme& scheme,
							   DiffWindows& diffWindows)
		: Core::Panel("changes", "Changes", "git", ICON_MDI_FILE_DOCUMENT_EDIT_OUTLINE), m_Model(model), m_Documents(documents),
		  m_DocumentWindows(documentWindows), m_Scheme(scheme), m_DiffWindows(diffWindows),
		  m_DiscardQuestion(modals, "Discard changes?")
	{
	}

	void ChangesPanel::OnUpdate(Core::Timestep)
	{
		// OnImGuiRender is what hands the gate back, and PanelManager skips it for a panel that
		// is hidden, collapsed or behind another dock tab - so a question left open when the
		// panel stopped being drawn would hold the gate forever and silence every modal in the
		// application. WasDrawn reports the previous frame, and a question can only be asked
		// from a frame this panel drew, so this never withdraws one the reader can still see.
		//
		// The cost is that the question is dropped, queue and all: a discard clicked while
		// another modal held the gate disappears without a word if the panel is hidden before
		// its turn comes. That is the lesser harm - nothing is discarded, and the row is still
		// there to click again, whereas a gate never handed back takes the whole application's
		// modals down with it and gives no way back at all.
		if (!WasDrawn() && m_DiscardQuestion.IsAsked())
		{
			m_DiscardQuestion.Withdraw();
		}

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
				// A directory is reported as itself, and the editor holds documents, not
				// directories - so the expectation has to name the files that went with it or
				// each one arrives as a change nobody in this application made. The list was
				// taken when the discard was confirmed, which is the last moment it existed;
				// see m_DiscardedFiles.
				//
				// A folder the reader never opened has no listing anywhere, so there is
				// nothing to expand it into and its files do arrive unannounced. That is the
				// honest answer: the only other source is the disk, which no longer has the
				// directory to read.
				const auto known = m_DiscardedFiles.find(path);

				if (known == m_DiscardedFiles.end())
				{
					absolute.push_back(m_Model.GetWorkingDirectory() / path);
					continue;
				}

				for (const std::filesystem::path& file : known->second)
				{
					absolute.push_back(m_Model.GetWorkingDirectory() / file);
				}

				m_DiscardedFiles.erase(known);
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

			// And the files a discard was going to report: a discard whose write failed
			// leaves its entry behind, and one kept across a workspace change would name
			// paths relative to a working directory that is no longer the one open.
			m_DiscardedFiles.clear();

			// The question goes with them, and its gate with it. Begin is the only other code
			// that hands the gate back, and it is below this return - a workspace closed while
			// a discard was on screen would otherwise leave the gate held forever, and no modal
			// question anywhere in the application could be asked again. The rows it was asked
			// about are gone in any case, so there is nothing left to answer.
			m_DiscardQuestion.Withdraw();

			ImGui::TextUnformatted("No repository.");
			return;
		}

		const ChangeSections sections = PartitionStatus(*status);

		// The lists scroll together above a commit box pinned to the bottom. Reserved by
		// height rather than drawn after, because the box must not move as the lists grow.
		// The write error's line is drawn between the two while there is one, so it is
		// reserved too - or the box would be pushed past the bottom of the panel.
		const float errorHeight =
			m_Model.GetWriteError().has_value() ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;

		const float boxHeight = CommitBoxHeight() + errorHeight + ImGui::GetStyle().ItemSpacing.y;

		if (ImGui::BeginChild("changes.lists", ImVec2(0.0f, -boxHeight)))
		{
			// An empty section is not drawn at all: a repository with no conflicts must not
			// carry an empty "Conflicted" heading forever.
			// A child without a border has no padding of its own, and a row's highlight reaches
			// half an item spacing above the row - so the first heading's would be cut off at
			// the top, leaving what is drawn on it looking pushed up against its highlight.
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y * 0.5f);

			bool drawnAbove = false;

			m_Section = Section::Conflicted;
			drawnAbove = RenderSection("CONFLICTED", sections.Conflicted, *status, palette,
									   drawnAbove);

			m_Section = Section::Staged;
			drawnAbove = RenderSection("STAGED", sections.Staged, *status, palette, drawnAbove) ||
						 drawnAbove;

			m_Section = Section::Unstaged;
			RenderSection("CHANGES", sections.Unstaged, *status, palette, drawnAbove);
		}

		ImGui::EndChild();

		RenderWriteError();
		RenderCommitBox();
		RenderDiscardConfirm();
	}

	bool ChangesPanel::RenderSection(const char* heading, const std::vector<std::size_t>& rows,
									 const Core::WorkingTreeStatus& status,
									 const GitPalette& palette, bool spaced)
	{
		if (rows.empty())
		{
			// A section that draws nothing still drew something: an empty list. Recorded here
			// rather than left stale, or the next non-empty frame would move the selection
			// against a list from before this section emptied.
			m_LastDrawn[static_cast<std::size_t>(m_Section)].clear();
			return false;
		}

		ImGui::PushID(heading);

		// A section after another keeps one item spacing more from it, so the lists read as
		// groups rather than as one run broken by a line of different text.
		if (spaced)
		{
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
		}

		const std::size_t slot = static_cast<std::size_t>(m_Section);

		// Drawn by hand rather than as a CollapsingHeader: that one paints a full-width
		// block with a triangle of its own, and the rows under it already open and close on
		// a chevron. A heading is one line of the same list, not a bar across it.
		const float rowHeight = ImGui::GetTextLineHeight();

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const ImVec2 rowScreenStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		if (ImGui::Selectable("##header", false, ImGuiSelectableFlags_AllowOverlap,
							  ImVec2(0.0f, rowHeight)))
		{
			m_Collapsed[slot] = !m_Collapsed[slot];
		}

		// Off the rectangle, for the reason RenderRow gives.
		const ImVec2 rowScreenEnd(rowScreenStart.x + rowWidth,
								  rowScreenStart.y + ImGui::GetItemRectSize().y);

		const bool rowHovered = ImGui::IsMouseHoveringRect(rowScreenStart, rowScreenEnd);

		const bool open = !m_Collapsed[slot];

		const float pad = RowPadding();
		const float textStart = rowStart.x + pad + IconColumnWidth() + TextGap();

		ImGui::SetCursorPos(ImVec2(rowStart.x + pad, rowStart.y));
		ImGui::TextDisabled("%s", open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT);

		ImGui::SetCursorPos(ImVec2(textStart, rowStart.y));
		ImGui::TextDisabled("%s", heading);

		// The count as a pill rather than in brackets: it is a figure about the heading,
		// not part of its name. Painted rather than laid out as an item, so the row still
		// ends on the one Dummy below.
		{
			ImGui::SameLine(0.0f, TextGap());

			const std::string count = std::format("{}", rows.size());
			const ImVec2 countSize = ImGui::CalcTextSize(count.c_str());
			const float pillPadding = ImGui::GetStyle().FramePadding.x;

			const ImVec2 pillMin = ImGui::GetCursorScreenPos();
			const ImVec2 pillMax(pillMin.x + countSize.x + pillPadding * 2.0f,
								 pillMin.y + rowHeight);

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(pillMin, pillMax, ImGui::GetColorU32(ImGuiCol_Button),
									rowHeight * 0.5f);
			drawList->AddText(ImVec2(pillMin.x + pillPadding, pillMin.y),
							  ImGui::GetColorU32(ImGuiCol_Text), count.c_str());
		}

		// Only under the mouse, as on the rows: a button on every heading at all times is
		// the loudest thing in the panel, and it is the least often wanted.
		if (rowHovered)
		{
			const char* allIcon = m_Section == Section::Staged ? ICON_MDI_MINUS : ICON_MDI_PLUS;

			ImGui::SetCursorPos(
				ImVec2(rowStart.x + rowWidth - pad - ButtonWidth(allIcon), rowStart.y));

			ImGui::BeginDisabled(m_Model.IsWriteInFlight());

			if (ImGui::SmallButton(allIcon))
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

			ImGui::SetItemTooltip("%s", m_Section == Section::Staged ? "Unstage all" : "Stage all");

			ImGui::EndDisabled();
		}

		// See the end of RenderRow for why a row laid out by hand ends on an item.
		ImGui::SetCursorPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		std::vector<std::filesystem::path> drawn;
		drawn.reserve(rows.size());

		for (const std::size_t entry : rows)
		{
			drawn.push_back(status.Entries[entry].Path);
		}

		if (m_Selected && m_Selected->InSection == m_Section && drawn != m_LastDrawn[slot])
		{
			const auto moved = NextSelection(m_Selected->Path, m_LastDrawn[slot], drawn);

			if (moved.has_value())
			{
				m_Selected = Selection{.InSection = m_Section, .Path = *moved};
			}
			else if (!SelectedIsInsideExpandedFolder())
			{
				// A row inside an open folder is drawn from that folder's listing and so is
				// in neither list NextSelection was given; it is not gone, it was never
				// counted. Left alone rather than reset, so the highlight goes on naming the
				// file whose diff is open. Closing the folder moves the selection onto the
				// folder row, because the click that closes it is a click on that row.
				m_Selected.reset();
			}
		}

		m_LastDrawn[slot] = drawn;

		if (open)
		{
			// One level in, so a row's icon sits under the heading's text: the rows belong to
			// the heading, and at the same indent the two read as siblings.
			ImGui::Indent(TreeIndent());

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

			ImGui::Unindent(TreeIndent());
		}

		ImGui::PopID();

		return true;
	}

	void ChangesPanel::RenderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
								 const GitPalette& palette)
	{
		const Core::FileStatus& file = status.Entries[entry];

		ImGui::PushID(static_cast<int>(entry));

		// One line: the name, then its directory, dimmed, in whatever room the name leaves.
		// The directory is there to tell two files of one name apart, which it still does cut
		// short, and a second line spent on it doubled the height of every list for the sake
		// of the few rows that needed it.
		const std::filesystem::path directory = file.Path.parent_path();

		const float rowHeight = ImGui::GetTextLineHeight();

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
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Right))
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

		const char letterText[] = {letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;

		const float pad = RowPadding();
		const float gap = TextGap();
		const float textStart = rowStart.x + pad + IconColumnWidth() + gap;
		const float rightEdge = rowStart.x + rowWidth - pad;

		// The letter sits on the right edge, and under the mouse the buttons take its place.
		// The text gives way to whichever of the two is there, so a long directory - or a name
		// too long for the row - is cut a little shorter while the mouse is on its row. Where
		// the name starts does not move.
		const float trailing = rowHovered ? buttonsWidth : letterWidth;
		const float available = rightEdge - trailing - ImGui::GetStyle().ItemSpacing.x - textStart;

		const std::string name = Core::ToUtf8(file.Path.filename());
		const std::string directoryText = Core::ToUtf8(directory);

		const RowText fit = FitRowText(
			available, ImGui::CalcTextSize(name.c_str()).x,
			directory.empty() ? 0.0f : ImGui::CalcTextSize(directoryText.c_str()).x, gap,
			MinDirectoryWidth());

		ImGui::SetCursorPos(ImVec2(rowStart.x + pad, rowStart.y));
		ImGui::TextUnformatted(Core::IconForExtension(Core::ToUtf8(file.Path.extension())));

		ImGui::SetCursorPos(ImVec2(textStart, rowStart.y));
		ImGui::TextColored(TintedName(letterColor), "%s", Elided(name, fit.Name).c_str());

		if (fit.Directory > 0.0f)
		{
			ImGui::SameLine(0.0f, gap);
			ImGui::TextDisabled("%s", Elided(directoryText, fit.Directory).c_str());
		}

		if (rowHovered)
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - buttonsWidth, rowStart.y));
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
		else
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - letterWidth, rowStart.y));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%s", letterText);
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
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_Selected = identity;
		}

		// Directly after the Selectable, as in RenderRow. The menu leaves out "Open File" of
		// its own accord, from file.IsDirectory.
		RenderRowContextMenu(file, letter);

		const bool expanded = m_Expanded.contains(file.Path);

		const char* chevron = expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;
		const char* folderIcon = expanded ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER;

		const char* stageIcon = m_Section == Section::Staged ? ICON_MDI_MINUS : ICON_MDI_PLUS;

		// The same rule a file row follows: discard only acts on the working tree, so it is
		// offered only where the working tree is what the row is showing.
		const bool offersDiscard = m_Section == Section::Unstaged;

		// Nothing in the index to restore a wholly untracked directory from, so this deletes.
		const char* discardIcon = file.IsUntracked ? ICON_MDI_DELETE : ICON_MDI_UNDO;

		const float discardWidth =
			offersDiscard ? ButtonWidth(discardIcon) + ImGui::GetStyle().ItemSpacing.x : 0.0f;

		const float buttonsWidth = discardWidth + ButtonWidth(stageIcon);

		const float gap = TextGap();

		const char letterText[] = {letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;

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

		// The chevron takes the icon column a file row gives its icon, and the folder icon and
		// name follow one level further in. Hanging the chevron out into the heading's column
		// instead, so the name could line up with the files', made the folder read as a
		// sibling of the heading rather than a row under it; and reserving a chevron column on
		// every row would push all the file rows right for the sake of the few that open.
		const float pad = RowPadding();
		const float iconStart = rowStart.x + pad + TreeIndent();
		const float textStart = iconStart + IconColumnWidth() + gap;
		const float rightEdge = rowStart.x + rowWidth - pad;

		// The same trade a file row makes between its letter and its buttons - see RenderRow.
		const float trailing = rowHovered ? buttonsWidth : letterWidth;

		// Cut short of whatever sits on the right edge and of the count.
		const float nameLimit = rightEdge - trailing - ImGui::GetStyle().ItemSpacing.x -
								countWidth - separatorWidth - textStart;

		// Dimmed like the heading's: the icon beside it already says open or closed, so the
		// chevron is only the affordance.
		ImGui::SetCursorPos(ImVec2(rowStart.x + pad, rowStart.y));
		ImGui::TextDisabled("%s", chevron);

		ImGui::SetCursorPos(ImVec2(iconStart, rowStart.y));
		ImGui::TextUnformatted(folderIcon);

		ImGui::SetCursorPos(ImVec2(textStart, rowStart.y));

		const std::string elided = Elided(Core::ToUtf8(file.Path.filename()), nameLimit);

		// A row too narrow for even an ellipsis of the name draws neither the separator nor
		// the count: neither says anything without the name, and both would run on under
		// the letter or the buttons.
		const bool drawsName = !elided.empty();

		if (drawsName)
		{
			ImGui::TextColored(TintedName(letterColor), "%s",
							   (elided + std::string(Separator)).c_str());
		}

		if (drawsName && !count.empty())
		{
			ImGui::SameLine(0.0f, gap);
			ImGui::TextDisabled("%s", count.c_str());
		}

		if (!rowHovered)
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - letterWidth, rowStart.y));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%s", letterText);
		}
		else
		{
			ImGui::SetCursorPos(ImVec2(rightEdge - buttonsWidth, rowStart.y));
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

		ImGui::Indent(TreeIndent());

		// RenderRow identifies a row by its index into the status it was handed, so a child
		// at index 0 and the outer status's entry 0 would be the same ImGui id. Scoped under
		// the folder's own path, which no other row in this panel draws under.
		ImGui::PushID(Core::ToUtf8(file.Path).c_str());

		for (std::size_t child = 0; child < listing->Entries.size(); ++child)
		{
			RenderRow(child, *listing, palette);
		}

		ImGui::PopID();
		ImGui::Unindent(TreeIndent());
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

		// A line between the lists and the box: the lists scroll and the box stays put, and
		// without it the two read as one column with a gap in it.
		ImGui::Separator();

		ImGui::BeginDisabled(inFlight);

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

		// Beside the button rather than on a line of its own: amend changes what the button
		// will do, so the two are read together.
		const float amendWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
								 ImGui::CalcTextSize("Amend").x;

		// Held above zero: ImGui reads a negative width as "all but this much" and zero as
		// "as wide as the label", so a panel narrower than the two together would otherwise
		// turn the button full-width at the very moment there is least room for it.
		const float commitWidth =
			std::max(width - amendWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);

		ImGui::BeginDisabled(!canCommit);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);

		// In the accent colour while it can be pressed: this is the one thing the panel exists
		// to let you do, and it should not look like one of the row controls. Grey while it
		// cannot, which says so better than a faded accent would.
		if (canCommit)
		{
			const ImVec4 accent = [&] {
				const Core::Color colour = ChromePaletteFor(m_Scheme.Get()).Accent;
				return ImVec4(colour.R, colour.G, colour.B, colour.A);
			}();

			ImGui::PushStyleColor(ImGuiCol_Button, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Scaled(accent, 1.15f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, Scaled(accent, 0.85f));

			// White whatever the scheme's text colour: a light scheme's dark text on its
			// accent is the one pairing that does not read.
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		}

		if (ImGui::Button(ICON_MDI_CHECK "  Commit", ImVec2(commitWidth, 0.0f)))
		{
			m_Model.RequestCommit({.Message = m_Message, .Amend = m_Amend});
		}

		if (canCommit)
		{
			ImGui::PopStyleColor(4);
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		ImGui::BeginDisabled(inFlight);
		ImGui::Checkbox("Amend", &m_Amend);
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

		// The same test the row itself applies before printing a count - see RenderFolderRow.
		// Held is not enough: the model keeps a listing until the next status refresh drops it,
		// so a folder the reader opened and then closed still has one, and a count taken from
		// it would put a number in the dialog that the row beneath is deliberately refusing to
		// show. Asking git for one here is no answer either, since it would arrive after the
		// question did, or never.
		if (file.IsDirectory && m_Expanded.contains(file.Path))
		{
			if (const std::shared_ptr<const Core::WorkingTreeStatus> listing =
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

		// Every discard asks. Git can hand back none of what one throws away - not a tracked
		// file's uncommitted edits, not an untracked file, not a directory - so there is no
		// case where going ahead silently is safe. The explorer has always confirmed every
		// delete; this panel was the one place that did not.
		//
		// Asked first, by subject: while this question is on screen it takes the clicks itself,
		// but one waiting its turn behind another modal leaves the row clickable, and a second
		// click there would queue a duplicate that asks again about a file the first answer has
		// already discarded.
		if (!m_DiscardQuestion.Asked(question))
		{
			m_DiscardQuestion.Ask(std::move(question));
		}
	}

	std::vector<std::filesystem::path> ChangesPanel::DirtyDocuments() const
	{
		std::vector<std::filesystem::path> dirty;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

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

		// Every ancestor, not just the immediate parent: a folder's listing is recursive, so
		// a selected row can be several directories deep under the one folder that was opened.
		std::filesystem::path directory = m_Selected->Path.parent_path();

		while (!directory.empty())
		{
			if (m_Expanded.contains(directory))
			{
				return true;
			}

			std::filesystem::path parent = directory.parent_path();

			// A path that is its own parent - a root - would otherwise loop forever. Status
			// paths are relative and never reach one, but nothing here forces that.
			if (parent == directory)
			{
				break;
			}

			directory = std::move(parent);
		}

		return false;
	}

	void ChangesPanel::RenderDiscardConfirm()
	{
		if (!m_DiscardQuestion.Begin())
		{
			return;
		}

		// Begin returning true is what guarantees there is a current payload.
		const DiscardQuestion& question = *m_DiscardQuestion.GetCurrent();

		// Worded when the discard was asked for - see DiscardQuestionText.
		ImGui::TextUnformatted(question.Text.c_str());

		ImGui::TextDisabled("This cannot be undone.");

		// A second, different loss: what git never saw, and so the one part of this no refresh
		// of the working tree could put back either.
		if (!question.Plan.Dirty.empty())
		{
			ImGui::Separator();
			ImGui::Text("%zu open file(s) have unsaved edits that would be lost.",
						question.Plan.Dirty.size());
		}

		ImGui::Separator();

		// Copied out before Answer(), which drops the payload `question` refers to.
		const DiscardRequest request{.Restore = question.Plan.Restore,
									 .Delete = question.Plan.Delete};

		const std::filesystem::path subject = question.Subject;
		const std::vector<std::filesystem::path> files = question.Plan.Files;

		// The same guard every other write control in this panel takes.
		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		if (ImGui::Button("Discard"))
		{
			// Remembered here, while the files still exist: TakeDiscarded reports the subject
			// - a directory, for a folder row - and by then there is nothing left on disk to
			// enumerate. See m_DiscardedFiles.
			if (!files.empty())
			{
				m_DiscardedFiles[subject] = files;
			}

			m_Model.RequestDiscard(request);
			m_DiscardQuestion.Answer();
		}

		ImGui::EndDisabled();

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_DiscardQuestion.Answer();
		}

		m_DiscardQuestion.End();
	}
}

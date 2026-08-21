#include "Git/ChangesPanel.h"

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"

#include <imgui.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>

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
	}

	ChangesPanel::ChangesPanel(GitModel& model, const Core::DocumentSet& documents,
							   DocumentWindows& documentWindows, const ActiveScheme& scheme,
							   DiffWindows& diffWindows)
		: Core::Panel("changes", "Changes"), m_Model(model), m_Documents(documents),
		  m_DocumentWindows(documentWindows), m_Scheme(scheme), m_DiffWindows(diffWindows)
	{
	}

	void ChangesPanel::OnImGuiRender()
	{
		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		// A commit that landed clears the box. Taken rather than inferred from the status
		// changing, which would also fire for a commit made in a terminal.
		if (m_Model.TakeCommitted().has_value())
		{
			m_Message.clear();
			m_Amend = false;
		}

		const std::shared_ptr<const Core::WorkingTreeStatus> status = m_Model.GetStatus();

		if (status == nullptr)
		{
			ImGui::TextUnformatted("No repository.");
			return;
		}

		const ChangeSections sections = PartitionStatus(*status);

		// The lists scroll together above a commit box pinned to the bottom. Reserved by
		// height rather than drawn after, because the box must not move as the lists grow.
		const float boxHeight = ImGui::GetFrameHeightWithSpacing() * 4.0f;

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
	}

	void ChangesPanel::RenderSection(const char* heading, const std::vector<std::size_t>& rows,
									 const Core::WorkingTreeStatus& status,
									 const GitPalette& palette)
	{
		if (rows.empty())
		{
			return;
		}

		ImGui::PushID(heading);

		const ImVec2 headerStart = ImGui::GetCursorPos();
		const float availableWidth = ImGui::GetContentRegionAvail().x;

		const std::string label = std::format("{}  ({})", heading, rows.size());

		// AllowOverlap: the "stage all"/"unstage all" button is laid over the same row via
		// SameLine below, the same technique the file rows and HitList's group headers use.
		const bool open = ImGui::CollapsingHeader(
			label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

		const char* allLabel = m_Section == Section::Staged ? "Unstage all" : "Stage all";
		const float buttonWidth =
			ImGui::CalcTextSize(allLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;

		ImGui::SameLine(headerStart.x + availableWidth - buttonWidth);
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

		if (open)
		{
			for (const std::size_t entry : rows)
			{
				RenderRow(entry, status, palette);
			}
		}

		ImGui::PopID();
	}

	void ChangesPanel::RenderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
								 const GitPalette& palette)
	{
		const Core::FileStatus& file = status.Entries[entry];

		ImGui::PushID(static_cast<int>(entry));

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		// An empty label over the whole row: the letter, name and directory are laid over it
		// in pieces below, and the +/- button is laid over its right edge. AllowOverlap keeps
		// every click reaching whichever of those three actually sits under the mouse.
		if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap))
		{
			const OpenDiff diff{
				.Commit = std::nullopt, .Target = TargetForSection(), .Path = file.Path};

			// Exactly one working-tree tab at a time: GitModel keeps a single slot for this
			// diff, so a second tab would intermittently show "that file is not in this
			// diff" as the two took turns asking into it. Only closed when this click asks
			// for a *different* diff than the one already open — Open() already re-docks and
			// re-raises an exact match, and closing first would throw that away, yanking a
			// tab the user dragged elsewhere back to the centre node on every click.
			if (!m_DiffWindows.IsOpen(diff))
			{
				m_DiffWindows.CloseWorkingTree();
			}

			m_DiffWindows.Open(diff);
		}

		// No popup at all outside Changes: BeginPopupContextItem would otherwise open an
		// empty box on a right-click over a staged or conflicted row.
		if (m_Section == Section::Unstaged)
		{
			if (ImGui::BeginPopupContextItem("##rowContext"))
			{
				if (ImGui::MenuItem("Discard changes..."))
				{
					// Task 10 turns this into a pending confirmation and only then calls
					// RequestDiscard — never straight from the menu, since a discard is
					// irreversible and the confirmation is what guards it.
				}

				ImGui::EndPopup();
			}
		}

		ImGui::SetCursorPos(rowStart);

		// A conflicted entry is not split into a staged and an unstaged half the way an
		// ordinary file is — see ChangesRows.h — so its letter is read off both sides
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

		const char letter = ChangeLetter(flags, file.IsUntracked);
		const ImU32 letterColor = ColourOfLetter(letter, palette);

		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(letterColor), "%c", letter);
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextUnformatted("  ");
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextUnformatted(Core::ToUtf8(file.Path.filename()).c_str());

		const std::filesystem::path directory = file.Path.parent_path();

		if (!directory.empty())
		{
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextDisabled("  %s", Core::ToUtf8(directory).c_str());
		}

		const char* buttonLabel = m_Section == Section::Staged ? "-" : "+";

		// The same measurement RenderSection uses for its own right-aligned button - a
		// SmallButton's width is CalcTextSize(label).x + FramePadding.x * 2, not a frame's
		// height, and the two must agree or the row and the header disagree about where the
		// right edge is.
		const float buttonWidth =
			ImGui::CalcTextSize(buttonLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;

		ImGui::SameLine(rowStart.x + rowWidth - buttonWidth);
		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		if (ImGui::SmallButton(buttonLabel))
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

		ImGui::PopID();
	}

	void ChangesPanel::RenderCommitBox()
	{
		const bool inFlight = m_Model.IsWriteInFlight();

		ImGui::BeginDisabled(inFlight);

		const ImVec2 boxSize(ImGui::GetContentRegionAvail().x,
							 ImGui::GetFrameHeightWithSpacing() * 2.0f);

		ImGui::InputTextMultiline("##commitMessage", m_Message.data(),
								  m_Message.size() + 1, boxSize,
								  ImGuiInputTextFlags_CallbackResize, ResizeMessageCallback,
								  &m_Message);

		ImGui::EndDisabled();

		ImGui::BeginDisabled(inFlight);
		ImGui::Checkbox("Amend", &m_Amend);
		ImGui::EndDisabled();

		ImGui::SameLine();

		// Whitespace-only counts as empty here - git would reject it the same way - but the
		// message sent to RequestCommit below is never trimmed; only this enablement test is.
		const bool canCommit = !inFlight && (!Core::Trimmed(m_Message).empty() || m_Amend);

		ImGui::BeginDisabled(!canCommit);

		if (ImGui::Button("Commit"))
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
}

#include "Git/ChangesPanel.h"

#include "Editor/DocumentWindows.h"

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Text/DocumentSet.h"

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

		// lexically_normal collapses "a/../b" without touching the filesystem, and
		// make_preferred turns git's forward slashes and a document's native separators into
		// the same spelling - the same normalisation ExpectedWrites applies before comparing,
		// and for the same reason: the two paths being compared come from different sources.
		std::filesystem::path Normalize(const std::filesystem::path& path)
		{
			std::filesystem::path normalized = path.lexically_normal();
			normalized.make_preferred();
			return normalized;
		}

		// Whole-path equality, case-folded, exactly what SamePath in ExpectedWrites.cpp does.
		// A filename or a suffix match is not safe here: a repository-root file has an empty
		// parent_path, so matching on "ends with" degenerates into "same name anywhere in the
		// tree" and would report an unrelated file elsewhere in the tree as dirty - or, worse,
		// fail to fire at all and let a real dirty buffer go unwarned.
		bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return Core::CompareNames(Core::ToUtf8(left), Core::ToUtf8(right)) == 0;
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

		// Registered only now, after the write has succeeded, so an expectation cannot be left
		// behind by a discard that failed - and so its short life starts as late as possible.
		if (std::vector<std::filesystem::path> discarded = m_Model.TakeDiscarded(); !discarded.empty())
		{
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
		RenderDiscardConfirm();
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
					AskAboutDiscard(file);
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

	void ChangesPanel::AskAboutDiscard(const Core::FileStatus& file)
	{
		m_PendingDiscard = {};

		// An untracked file has nothing in the index to be restored from, so discarding it can
		// only mean deleting it - a different outcome, named separately in the dialog.
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

			const std::filesystem::path normalizedItem = Normalize(item.Path);

			for (const std::filesystem::path& path : paths)
			{
				// `path` is git-relative; a document's path is absolute. Absolutised against
				// the repository's working directory - not GetRoot(), the folder the user
				// opened, which Core::Repository::Open may have resolved upward from - before
				// comparing whole normalised paths. The same rule ExpectedWrites uses, and not
				// the filename-and-ends_with shortcut that was already withdrawn from
				// ExpectedWrites for making a root-level file match anywhere in the tree while
				// never matching a nested one at all.
				const std::filesystem::path normalizedPath =
					Normalize(m_Model.GetWorkingDirectory() / path);

				if (SamePath(normalizedItem, normalizedPath))
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

		if (!m_PendingDiscard.Restore.empty())
		{
			ImGui::TextUnformatted("These will go back to the state in the index:");

			for (const std::filesystem::path& path : m_PendingDiscard.Restore)
			{
				ImGui::BulletText("%s", Core::ToUtf8(path).c_str());
			}
		}

		if (!m_PendingDiscard.Delete.empty())
		{
			ImGui::TextUnformatted("These will be deleted:");

			const GitPalette palette = ReadGitPalette(m_Scheme.Get());
			const ImVec4 deleteColor = ImGui::ColorConvertU32ToFloat4(palette.GutterRemoved);

			for (const std::filesystem::path& path : m_PendingDiscard.Delete)
			{
				ImGui::TextColored(deleteColor, "-  %s", Core::ToUtf8(path).c_str());
			}
		}

		if (!m_PendingDirty.empty())
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("Open with unsaved changes - those changes will be lost:");

			for (const std::filesystem::path& path : m_PendingDirty)
			{
				// The full path, not just the filename: two same-named files open from
				// different directories would otherwise make this warning ambiguous exactly
				// where precision matters.
				ImGui::BulletText("%s", Core::ToUtf8(path).c_str());
			}
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

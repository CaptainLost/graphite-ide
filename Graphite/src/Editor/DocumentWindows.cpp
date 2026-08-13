#include "Editor/DocumentWindows.h"

#include "Editor/TextEditorWidget.h"

#include "Lang/Registry/LanguageRegistry.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/FileSync.h"
#include "Core/Text/TextDocument.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr double kTransientMessageSeconds = 4.0;

		std::string Join(const std::vector<std::string>& values)
		{
			std::string joined;

			for (std::size_t index = 0; index < values.size(); ++index)
			{
				if (index > 0)
					joined += ", ";

				joined += values[index];
			}

			return joined;
		}

		std::string WindowName(const Core::DocumentSet& documents,
							   std::size_t index)
		{
			// ImGui shows what precedes ### and identifies by what follows, so the path — the
			// only identity that cannot collide — stays out of sight, and the icon in front
			// of the title moves no window id, dock position or imgui.ini entry.
			//
			// The MDI glyphs are merged onto the regular mono face alone, so nothing drawing
			// this title may push another font.
			const std::filesystem::path& path = documents.At(index).Path;

			return std::string{Core::IconForExtension(Core::ToUtf8(path.extension()))} + "  " + documents.DisplayTitle(index) + "###" + Core::ToUtf8(path);
		}

		std::string Identity(const Core::DocumentSet& documents, std::size_t index)
		{
			return Core::ToUtf8(documents.At(index).Path);
		}

		std::vector<std::string> CurrentIdentities(const Core::DocumentSet& documents)
		{
			std::vector<std::string> identities;
			identities.reserve(documents.GetCount());

			for (std::size_t index = 0; index < documents.GetCount(); ++index)
				identities.push_back(Identity(documents, index));

			return identities;
		}

		bool LiesInAnyOf(const std::vector<std::filesystem::path>& directories,
						 const std::filesystem::path& path)
		{
			return std::find(directories.begin(), directories.end(), path.parent_path()) != directories.end();
		}

		std::string FileNameOf(const std::filesystem::path* path)
		{
			return path != nullptr ? Core::ToUtf8(path->filename()) : std::string{};
		}

		std::string SavedCount(std::size_t saved)
		{
			if (saved == 0)
				return {};

			return "Saved " + std::to_string(saved) + (saved == 1 ? " file." : " files.");
		}

		// Named together in one message rather than one message per file overwriting the
		// last, so what still needs attention is not buried under "saved".
		std::string ConflictSummary(const Core::SaveAllReport& report)
		{
			std::vector<std::string> names;
			names.reserve(report.Conflicted.size());

			for (const std::filesystem::path& path : report.Conflicted)
			{
				names.push_back(Core::ToUtf8(path.filename()));
			}

			std::string message = SavedCount(report.Saved);

			if (!message.empty())
			{
				message += " ";
			}

			message += std::to_string(names.size()) + " changed on disk and " + (names.size() == 1 ? "was" : "were") + " not saved: " + Join(names);

			return message;
		}
	}

	void DocumentWindows::RequestOpen(const std::filesystem::path& path, bool permanent)
	{
		const Core::OpenOutcome outcome = m_Documents.Open(path, permanent);

		// The set has activated it, but nothing about that reaches ImGui, which goes on
		// showing whichever tab it last focused.
		if (outcome == Core::OpenOutcome::Activated)
			m_FocusRequest = path;

		if (outcome != Core::OpenOutcome::NeedsConfirmation)
			return;

		AskAboutSize(path, permanent);
	}

	void DocumentWindows::LoadConfirmed(const std::filesystem::path& path, bool permanent)
	{
		const auto existing = m_Documents.Find(path);

		// A tab can already be there with nothing in it: one opened on a file that did not
		// exist, whose file has since appeared and appeared large. OpenConfirmed would only
		// activate that tab, so the item is filled directly instead.
		if (existing && !m_Documents.At(*existing).Document)
		{
			// Confirmed, so NeedsConfirmation cannot come back, and Loaded and Failed both
			// leave the tab saying the right thing: the content, or the reason there is none.
			static_cast<void>(Core::LoadFromFile(m_Documents.At(*existing), true));
			return;
		}

		m_Documents.OpenConfirmed(path, permanent);
	}

	void DocumentWindows::AskAboutSize(const std::filesystem::path& path, bool permanent)
	{
		LargeFileQuestion question{path, permanent, 0};

		// Probed once, here, rather than every frame the modal is open: a stat plus an 8 KB
		// read per frame is real work on a network share. Whoever asks has just probed the
		// same file successfully, so a failure here means the file went away in between.
		if (const auto probe = Core::ProbeFile(path))
		{
			question.size = probe.GetValue().Size;
		}

		// Two events naming the same file — a double dispatch from the tree, a watcher batch
		// and a focus regain — are one question, not the same one asked twice in a row.
		if (!m_SizeQuestion.Asked(question))
		{
			m_SizeQuestion.Ask(std::move(question));
		}
	}

	void DocumentWindows::HandleRootMoved(const std::filesystem::path& from,
										  const std::filesystem::path& to)
	{
		const std::vector<std::string> oldIdentities = CurrentIdentities(m_Documents);

		m_Documents.RewritePaths(from, to);

		const std::vector<std::string> newIdentities = CurrentIdentities(m_Documents);

		// RewritePaths only ever changes paths in place, never the count or the order, so the
		// two snapshots line up index for index.
		for (std::size_t index = 0; index < newIdentities.size(); ++index)
		{
			if (newIdentities[index] != oldIdentities[index])
				MovePendingDockTarget(oldIdentities[index], newIdentities[index]);
		}
	}

	unsigned int DocumentWindows::DockNodeForIdentity(const std::string& identity) const
	{
		// The live window, if this document was ever drawn under this identity, is
		// authoritative for where it currently sits.
		if (ImGuiWindow* window = ImGui::FindWindowByName(identity.c_str());
			window != nullptr && window->DockNode != nullptr)
		{
			return window->DockNode->ID;
		}

		// The root moved again before a Render ever consumed the previous move's target.
		if (const auto pending = m_PendingDockTargets.find(identity);
			pending != m_PendingDockTargets.end())
		{
			return pending->second;
		}

		return 0;
	}

	void DocumentWindows::MovePendingDockTarget(const std::string& oldIdentity,
												const std::string& newIdentity)
	{
		const unsigned int target = DockNodeForIdentity(oldIdentity);

		m_PendingDockTargets.erase(oldIdentity);

		if (target != 0)
			m_PendingDockTargets[newIdentity] = target;
	}

	void DocumentWindows::HandleFilesChanged(
		const std::vector<std::filesystem::path>& directories, bool everything)
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			// The watcher names directories, so an item is worth a stat only when its own
			// directory is one of them. `everything` means the batch could not be narrowed
			// down at all — dropped notifications, or a git state change.
			if (!everything && !LiesInAnyOf(directories, m_Documents.At(index).Path))
			{
				continue;
			}

			RespondToSync(index, Core::SyncWithFile(m_Documents, index));
		}
	}

	void DocumentWindows::NotifyWindowFocus(bool focusedNow)
	{
		const bool regained = focusedNow && !m_WasFocused;
		m_WasFocused = focusedNow;

		if (!regained)
		{
			return;
		}

		SyncEveryDocument();
	}

	void DocumentWindows::SyncEveryDocument()
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			RespondToSync(index, Core::SyncWithFile(m_Documents, index));
		}
	}

	void DocumentWindows::RespondToSync(std::size_t index, Core::FileSyncOutcome outcome)
	{
		switch (outcome)
		{
		case Core::FileSyncOutcome::NeedsDecision:
		{
			// Asked rather than reloaded, and queued, so several files changing in one batch
			// (a git checkout) are asked about one at a time instead of every modal fighting
			// to open this frame.
			const std::filesystem::path path = m_Documents.At(index).Path;

			if (!m_ReloadConflict.Asked(path))
			{
				m_ReloadConflict.Ask(path);
			}

			break;
		}

		case Core::FileSyncOutcome::SafeToReload:
			ReloadOne(index);
			break;

		case Core::FileSyncOutcome::NeedsLoad:
		{
			// The file a tab was opened on before it existed is here now, and no later sync
			// would look again.
			Core::OpenItem& item = m_Documents.At(index);

			if (Core::LoadFromFile(item, false) == Core::ItemLoad::NeedsConfirmation)
			{
				AskAboutSize(item.Path, !item.Preview);
			}

			break;
		}

		case Core::FileSyncOutcome::Unchanged:
		case Core::FileSyncOutcome::WentMissing:
			// A file that went away is already said by the tab's own title, which gains and
			// loses "(deleted)" with the mark.
			break;
		}
	}

	void DocumentWindows::ReloadOne(std::size_t index)
	{
		const std::string fileName = Core::ToUtf8(m_Documents.At(index).Path.filename());

		if (const auto result = Core::ReloadItem(m_Documents, index))
		{
			SetTransientMessage(fileName + " reloaded (changed on disk)");
		}
		else
		{
			SetTransientMessage("Could not reload " + fileName + ": " + result.GetError().Message);
		}
	}

	bool DocumentWindows::SaveOne(std::size_t index)
	{
		const Core::OpenItem& item = m_Documents.At(index);

		// Checked here rather than left to SaveDocument, which succeeds having written
		// nothing: "Widget.obj saved" would be a message about a write that never happened.
		if (!item.Document)
		{
			return true;
		}

		const std::string fileName = Core::ToUtf8(item.Path.filename());

		if (const auto result = Core::SaveDocument(m_Documents, index))
		{
			SetTransientMessage(fileName + " saved");
			return true;
		}
		else
		{
			SetTransientMessage("Could not save " + fileName + ": " + result.GetError().Message);
			return false;
		}
	}

	void DocumentWindows::RequestSave(std::size_t index)
	{
		Core::OpenItem& item = m_Documents.At(index);

		if (!item.Document)
			return;

		if (item.Document->CheckChangedOnDisk())
		{
			// A change nobody was told about — a dropped notification, with no chance to have
			// asked earlier. This is the last safety net before the write.
			if (!m_OverwriteConfirm.Asked(item.Path))
			{
				m_OverwriteConfirm.Ask(item.Path);
			}

			return;
		}

		SaveOne(index);
	}

	bool DocumentWindows::SaveAllModified()
	{
		// Qualified: the member has the same name, and an unqualified call from inside it
		// would find itself.
		const Core::SaveAllReport report = Core::SaveAllModified(m_Documents);

		// A stopped batch says why, naming the document that stopped it, and gets no summary
		// over the top: a count of what was saved before the failure would only bury the one
		// thing the user has to act on.
		if (report.StoppedAt)
		{
			SetTransientMessage("Could not save " + Core::ToUtf8(report.StoppedAt->Path.filename()) + ": " + report.StoppedAt->Error.Message);

			return report.IsComplete();
		}

		if (report.Conflicted.empty())
		{
			if (report.Saved > 0)
			{
				SetTransientMessage(SavedCount(report.Saved));
			}

			return report.IsComplete();
		}

		// Nothing is asked about here: a conflicted document gets its Overwrite/Cancel
		// question from RequestSave, a deliberate per-document Ctrl+S.
		SetTransientMessage(ConflictSummary(report));

		return report.IsComplete();
	}

	bool DocumentWindows::HasModifiedDocuments() const
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.Document && item.Document->IsModified())
			{
				return true;
			}
		}

		return false;
	}

	void DocumentWindows::RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys)
	{
		commands.Add({.Id = "document.saveAll",
					  .Label = "Save All",
					  .Run = [this] { (void)SaveAllModified(); },
					  .Enabled = [this] { return HasModifiedDocuments(); }});

		keys.SetDefault("document.saveAll",
						{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});
	}

	void DocumentWindows::SetTransientMessage(std::string text)
	{
		m_TransientMessage =
			TransientMessage{std::move(text), ImGui::GetTime() + kTransientMessageSeconds};
	}

	std::optional<std::string> DocumentWindows::GetStatusMessage() const
	{
		if (!m_TransientMessage)
			return std::nullopt;

		if (ImGui::GetTime() >= m_TransientMessage->expiresAt)
			return std::nullopt;

		return m_TransientMessage->text;
	}

	void DocumentWindows::CloseAndForgetLayout(std::size_t index)
	{
		// The layout entry goes with the window: position belongs to the group, not to the
		// file, so a reopened document lands where the user is working rather than where it
		// was months ago — and imgui.ini stops collecting a dead section per file.
		ImGui::ClearWindowSettings(WindowName(m_Documents, index).c_str());
		m_Syntax.erase(m_Documents.At(index).Path);
		m_Documents.Close(index);
	}

	void DocumentWindows::CloseIfOpen(const std::filesystem::path& path)
	{
		if (const auto index = m_Documents.Find(path))
		{
			CloseAndForgetLayout(*index);
		}
	}

	void DocumentWindows::RenderSizeQuestion()
	{
		if (!m_SizeQuestion.Begin())
		{
			return;
		}

		const LargeFileQuestion* question = m_SizeQuestion.GetCurrent();
		const std::uint64_t size = question != nullptr ? question->size : 0;

		// A size of zero means the probe in AskAboutSize failed, which after a successful one
		// moments earlier means the file has gone.
		if (size == 0)
		{
			ImGui::TextUnformatted("This file can no longer be read. Load it anyway?");
		}
		else
		{
			ImGui::Text("This file is %llu MB. Load it anyway?",
						static_cast<unsigned long long>(size / (1024 * 1024)));
		}
		ImGui::Separator();

		if (ImGui::Button("Load"))
		{
			// Before Answer(), which drops the payload this still points into.
			if (question != nullptr)
			{
				LoadConfirmed(question->path, question->permanent);
			}

			m_SizeQuestion.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_SizeQuestion.Answer();
		}

		m_SizeQuestion.End();
	}

	void DocumentWindows::RenderReloadConflict()
	{
		if (!m_ReloadConflict.Begin())
		{
			return;
		}

		const std::filesystem::path* path = m_ReloadConflict.GetCurrent();

		ImGui::Text("%s changed on disk, and this tab has unsaved changes.",
					FileNameOf(path).c_str());
		ImGui::Separator();

		if (ImGui::Button("Reload from disk (lose my changes)"))
		{
			if (path != nullptr)
			{
				if (const auto index = m_Documents.Find(*path))
				{
					ReloadOne(*index);
				}
			}

			m_ReloadConflict.Answer();
		}

		ImGui::SameLine();

		// The entire acknowledgement mechanism: recording the file's current state as known
		// is what stops Ctrl+S, or the next sweep, asking about this file again.
		if (ImGui::Button("Keep my version"))
		{
			if (path != nullptr)
			{
				if (const auto index = m_Documents.Find(*path);
					index && m_Documents.At(*index).Document)
				{
					m_Documents.At(*index).Document->AcceptDiskState();
				}
			}

			m_ReloadConflict.Answer();
		}

		m_ReloadConflict.End();
	}

	void DocumentWindows::RenderOverwriteConfirm()
	{
		if (!m_OverwriteConfirm.Begin())
		{
			return;
		}

		const std::filesystem::path* path = m_OverwriteConfirm.GetCurrent();

		ImGui::Text("%s changed on disk since it was opened. Overwrite it?",
					FileNameOf(path).c_str());
		ImGui::Separator();

		if (ImGui::Button("Overwrite"))
		{
			if (path != nullptr)
			{
				if (const auto index = m_Documents.Find(*path))
				{
					SaveOne(*index);
				}
			}

			m_OverwriteConfirm.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_OverwriteConfirm.Answer();
		}

		m_OverwriteConfirm.End();
	}

	bool DocumentWindows::StillNeedsCloseConfirmation(const std::filesystem::path& path)
	{
		const auto index = m_Documents.Find(path);

		if (!index)
		{
			return false;
		}

		const Core::OpenItem& item = m_Documents.At(*index);

		if (item.Document && item.Document->IsModified())
		{
			return true;
		}

		// The question went stale while it waited its turn — the document was saved with
		// Ctrl+S, say — but the close button the user pressed still stands, so the tab is
		// closed here rather than merely dropped.
		CloseAndForgetLayout(*index);

		return false;
	}

	void DocumentWindows::RenderCloseConfirm()
	{
		if (!m_CloseConfirm.Begin([this](const std::filesystem::path& candidate) {
				return StillNeedsCloseConfirmation(candidate);
			}))
		{
			return;
		}

		const std::filesystem::path* path = m_CloseConfirm.GetCurrent();

		ImGui::Text("Save changes to %s before closing?", FileNameOf(path).c_str());
		ImGui::Separator();

		// Called before Answer(), which drops the payload `path` points into.
		const auto finishClose = [this, path] {
			if (path != nullptr)
			{
				CloseIfOpen(*path);
			}
		};

		if (ImGui::Button("Save"))
		{
			bool saved = true;

			if (path != nullptr)
			{
				if (const auto index = m_Documents.Find(*path))
				{
					saved = SaveOne(*index);
				}
			}

			// A failed save changes nothing: the document stays open and dirty, the reason is
			// already in the status bar, and the question stays up.
			if (saved)
			{
				finishClose();
				m_CloseConfirm.Answer();
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Discard"))
		{
			finishClose();
			m_CloseConfirm.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_CloseConfirm.Answer();
		}

		m_CloseConfirm.End();
	}

	bool DocumentWindows::IsNewSinceLastFrame(const std::string& identity) const
	{
		// Whether ImGui has ever heard of this window is the wrong question:
		// ClearWindowSettings undocks a window but does not destroy it, so a document closed
		// and reopened reuses the same ImGuiWindow object, stripped of its dock and with
		// nothing left to redock it. New *since last frame* is false for a document that has
		// been open continuously, which is what stops a user's drag being fought every frame.
		return std::find(m_PreviousIdentities.begin(), m_PreviousIdentities.end(), identity) == m_PreviousIdentities.end();
	}

	unsigned int DocumentWindows::ChooseDockTarget(const std::string& identity,
												   unsigned int fallbackDockNode)
	{
		// Checked, not merely non-zero: splitting the editor area, opening a file in the new
		// split and closing it leaves ImGui free to delete the emptied node, and
		// DockBuilderDockWindow does not fail on a dead id — it creates a fresh node under no
		// parent, so the window floats free instead of landing in the centre.
		const bool lastFocusedAlive =
			m_LastFocusedNode != 0 && ImGui::DockBuilderGetNode(m_LastFocusedNode) != nullptr;

		if (!lastFocusedAlive)
			m_LastFocusedNode = 0;

		// A window carrying over a dock position from before its path was rewritten takes
		// priority over both: it is more specific than "wherever was last focused", and right
		// even when nothing has been focused yet.
		if (const auto pending = m_PendingDockTargets.find(identity);
			pending != m_PendingDockTargets.end())
		{
			const unsigned int target = pending->second;
			m_PendingDockTargets.erase(pending);

			return target;
		}

		return lastFocusedAlive ? m_LastFocusedNode : fallbackDockNode;
	}

	void DocumentWindows::ActivateAndRememberNode(std::size_t index)
	{
		m_Documents.Activate(index);

		if (ImGuiWindow* window = ImGui::GetCurrentWindow();
			window != nullptr && window->DockNode != nullptr)
			m_LastFocusedNode = window->DockNode->ID;
	}

	SyntaxCache& DocumentWindows::SyntaxFor(const Core::OpenItem& item)
	{
		const auto [entry, inserted] = m_Syntax.try_emplace(item.Path);

		if (inserted)
		{
			entry->second.SetLexer(LexerFor(LanguageOfPath(item.Path)));
		}

		return entry->second;
	}

	// Drops every m_Syntax entry whose key is not the path of a currently open document. Run
	// once a frame rather than at the one place a path changes: m_Syntax is a side-map keyed on
	// an identity DocumentSet owns and can rewrite (a rename on disk, or of a directory an open
	// document lives under) without this map hearing about it, so an entry orphaned that way
	// would otherwise hold its std::vector<LineState> — one Continuation and one std::string
	// per line of the file — for the rest of the process. SyntaxFor already does the right
	// thing after a rename (a fresh cache under the new path, lexed with whatever language the
	// new extension implies), so this only ever removes what nothing can reach any more.
	void DocumentWindows::PruneSyntaxCaches()
	{
		for (auto it = m_Syntax.begin(); it != m_Syntax.end();)
		{
			if (m_Documents.Find(it->first))
			{
				++it;
			}
			else
			{
				it = m_Syntax.erase(it);
			}
		}
	}

	void DocumentWindows::ForgetLayoutOfWindowsClosedElsewhere(
		const std::vector<std::string>& startingIdentities)
	{
		for (const std::string& previousIdentity : m_PreviousIdentities)
		{
			if (std::find(startingIdentities.begin(), startingIdentities.end(), previousIdentity) == startingIdentities.end())
				ImGui::ClearWindowSettings(previousIdentity.c_str());
		}
	}

	void DocumentWindows::RenderDocumentWindows(
		const CommandContext& context, float fontSize, Core::IndentStyle indentStyle,
		unsigned int fallbackDockNode, bool wordWrap,
		const std::optional<std::filesystem::path>& focusRequest)
	{
		for (std::size_t index = 0; index < m_Documents.GetCount();)
		{
			Core::OpenItem& item = m_Documents.At(index);
			const std::string identity = Identity(m_Documents, index);
			const std::string name = WindowName(m_Documents, index);

			if (IsNewSinceLastFrame(identity))
			{
				if (const unsigned int targetNode = ChooseDockTarget(identity, fallbackDockNode);
					targetNode != 0)
					ImGui::DockBuilderDockWindow(name.c_str(), targetNode);
			}

			const bool preview = item.Preview;

			// Colour alone marks a preview, and it has to be pushed before Begin: Begin ->
			// BeginDocked calls StoreDockStyleForWindow, which snapshots the style colours in
			// effect at that moment, and the tab bar swaps to each window's own snapshot
			// before drawing its tab. A font cannot do the same — every tab of a node is
			// drawn in one pass with one global font — and the icon in the title comes from
			// the mono face only.
			if (preview)
			{
				ImGui::PushStyleColor(ImGuiCol_Text,
									  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}

			// The dot next to the tab, and — for a docked tab — what stops ImGui assuming the
			// X means "close now": the window is torn down from the dock's bookkeeping only
			// if Render stops resubmitting it, which the dirty-close branch below refuses to
			// do until the question resolves.
			const bool modified = item.Document && item.Document->IsModified();
			const ImGuiWindowFlags windowFlags =
				modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;

			// Begin() is what turns this into the selected tab of a docked window, which
			// SetWindowFocus called from RequestOpen could not do against a window not yet
			// submitted this frame.
			if (focusRequest == item.Path)
				ImGui::SetNextWindowFocus();

			bool open = true;
			const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

			// Popped the moment Begin returns, so the dimming reaches this document's tab and
			// title bar without following it into the editor's own contents.
			if (preview)
				ImGui::PopStyleColor();

			if (visible)
			{
				if (ImGui::IsWindowFocused())
				{
					ActivateAndRememberNode(index);
				}

				if (preview && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
					m_Documents.Promote(index);

				EditorView view;
				view.FontSize = fontSize;
				view.Indent = indentStyle;
				view.WordWrap = wordWrap;
				view.Syntax = &SyntaxFor(item);

				const EditorFrameResult editorResult = DrawEditor(context, item, view);

				// Once the user has typed into a preview it is no longer something they were
				// just glancing at.
				if (editorResult.Edited && preview)
					m_Documents.Promote(index);

				if (editorResult.SaveRequested)
					RequestSave(index);
			}

			ImGui::End();

			if (!open)
			{
				if (modified)
				{
					// Both versions are somebody's work: not closed here. The tab keeps being
					// submitted every frame until its turn comes up in the queue and Save,
					// Discard or Cancel resolves it. Queued rather than replacing whatever is
					// already pending, so a second tab's close button still gets its own
					// question, in order.
					if (!m_CloseConfirm.Asked(item.Path))
					{
						m_CloseConfirm.Ask(item.Path);
					}
				}
				else
				{
					CloseAndForgetLayout(index);
					continue;
				}
			}

			++index;
		}
	}

	void DocumentWindows::Render(const CommandContext& context, float fontSize,
								 Core::IndentStyle indentStyle,
								 unsigned int fallbackDockNode, bool wordWrap)
	{
		const std::vector<std::string> startingIdentities = CurrentIdentities(m_Documents);

		// Taken and cleared in one step: whatever asked for this focus did so earlier in this
		// same pass, so a request that matches no document this frame never will, and leaving
		// it set would hand a stolen focus to whichever document next claims that path.
		const std::optional<std::filesystem::path> focusRequest =
			std::exchange(m_FocusRequest, std::nullopt);

		// Every question is drawn here, before the per-document loop, because OpenPopup and
		// BeginPopupModal must not run from inside a Begin/BeginChild pair that may not be
		// current when the popup needs to draw. The loop below only ever asks them.
		RenderSizeQuestion();
		RenderReloadConflict();
		RenderOverwriteConfirm();
		RenderCloseConfirm();

		RenderDocumentWindows(context, fontSize, indentStyle, fallbackDockNode, wordWrap,
							  focusRequest);

		// Once per frame, not once per document: a prune that ran inside the per-document loop
		// above would cost a walk of m_Syntax for every open document instead of one for the
		// whole frame.
		PruneSyntaxCaches();

		// After the loop, not before: a renamed document's pending dock target has to be
		// consumed by Begin() above before its old identity's node can be undocked and
		// possibly deleted here, or the renamed document is stranded in a fresh orphan node.
		ForgetLayoutOfWindowsClosedElsewhere(startingIdentities);

		m_PreviousIdentities = CurrentIdentities(m_Documents);
	}
}

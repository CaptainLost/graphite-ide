#include "Editor/DocumentWindows.h"

#include "Editor/DefinitionNavigation.h"
#include "Editor/Palettes.h"
#include "Editor/TextEditorWidget.h"

#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerRegistry.h"

#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/FileSync.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
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

		// How long before a symbols request that came back Failed is tried again. A server is
		// routinely still starting or still indexing when the first one goes out, and its answer
		// then says nothing about whether a later one would succeed — but retrying on the next
		// frame would be sixty requests a second at a server that is busy.
		constexpr double kSymbolsRetrySeconds = 2.0;

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
			AnnounceReload(m_Documents.At(index));

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
			// Only on a write that actually happened: a didSave for a save that failed would tell
			// the server the file on disk matches what it has been sent, which is the one thing
			// that is not true. Qualified against Core::SaveDocument above, which is the free
			// function that does the writing.
			m_LanguageServices.SaveDocument(item.Path, LanguageOfPath(item.Path));

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
		// After the close, so the prune sees the document gone and tells the server so. Erasing
		// here directly would be the same thing minus the CloseDocument the prune sends.
		PruneLensStates();
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
			// Keeps the cache's width equal to the item's when a fresh cache is inserted for an
			// item that was already at a non-default width — the case being a path rewrite
			// while the document stays open: RewritePaths changes item.Path (a rename of the
			// file or of a directory it lives under), this try_emplace then inserts under the
			// new key with `inserted` true, and without this seed the new cache would sit at
			// Core::DefaultTabWidth forever. DrawEditor's compare-and-rebuild block in
			// TextEditorWidget.cpp only fires on item.TabWidth != view.Indent.Width, which is
			// already false the moment the item's own width was set correctly earlier — so
			// nothing would ever re-lex the cache at the right width.
			entry->second.SetTabWidth(item.TabWidth);
		}

		return entry->second;
	}

	// Drops every m_Syntax entry whose key is not the path of a currently open document. Run
	// once a frame rather than at the one place a path changes: m_Syntax is a side-map keyed on
	// an identity DocumentSet owns and can rewrite (a rename on disk, or of a directory an open
	// document lives under) without this map hearing about it, so an entry orphaned that way
	// would otherwise hold its std::vector<LineState> — one mode word and one std::string
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

		// Same map, same reason: m_CaretScreen is keyed on a path DocumentSet can rewrite without
		// telling it, and an entry orphaned that way would otherwise sit here for good.
		for (auto it = m_CaretScreen.begin(); it != m_CaretScreen.end();)
		{
			if (m_Documents.Find(it->first))
			{
				++it;
			}
			else
			{
				it = m_CaretScreen.erase(it);
			}
		}
	}

	DocumentWindows::LensState* DocumentWindows::LensFor(const Core::OpenItem& item)
	{
		const LanguageId language = LanguageOfPath(item.Path);

		// The one test for "a server serves this document". A language with no row in the server
		// registry gets no lens state at all, which is what puts a null LensModel on EditorView.
		if (ServerConfigFor(language) == nullptr || !item.Document)
		{
			return nullptr;
		}

		const auto [entry, inserted] = m_Lenses.try_emplace(item.Path);

		if (inserted)
		{
			// EnsureStarted before OpenDocument, in that order: OpenDocument is a no-op while no
			// server runs for this root, and every answer afterwards would be about a file the
			// server was never told exists. Both are safe to repeat, but this is the one moment
			// per document they are needed.
			m_LanguageServices.EnsureStarted(item.Path, language);
			m_LanguageServices.OpenDocument(item.Path, language, item.Document->GetBuffer().Materialize());
		}

		return &entry->second;
	}

	// The same prune, for the same reason, as PruneSyntaxCaches next door — see its comment. The
	// server is told the document closed from here as well, so its copy does not outlive the tab:
	// a path that has gone from DocumentSet is one nothing will ask about again.
	void DocumentWindows::PruneLensStates()
	{
		for (auto it = m_Lenses.begin(); it != m_Lenses.end();)
		{
			if (m_Documents.Find(it->first))
			{
				++it;
			}
			else
			{
				m_LanguageServices.CloseDocument(it->first, LanguageOfPath(it->first));
				it = m_Lenses.erase(it);
			}
		}
	}

	void DocumentWindows::DropInFlightCounts(const std::filesystem::path& path, LensState& state)
	{
		// The rows in flight were asked about before the text moved, so their answers are about
		// positions that no longer hold. Dropping the tickets means those answers are ignored when
		// they arrive, and clearing the set lets the rows be asked about again.
		state.Asked.clear();

		for (auto it = m_PendingCounts.begin(); it != m_PendingCounts.end();)
		{
			if (it->second.Path == path)
			{
				m_LanguageServices.Cancel(it->first);
				it = m_PendingCounts.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void DocumentWindows::AnnounceReload(const Core::OpenItem& item)
	{
		if (!item.Document)
		{
			return;
		}

		const LanguageId language = LanguageOfPath(item.Path);

		// The only other didChange goes out from UpdateLenses, for a frame the editor itself
		// edited. Without this one a file replaced from disk leaves the server holding the text
		// from before the reload for as long as the tab stays open: F12 resolves a position
		// against text nobody has, counts anchor to declarations that have moved, and the status
		// bar says "ready" throughout, because from the server's side nothing went wrong.
		m_LanguageServices.ChangeDocument(item.Path, language,
										  item.Document->GetBuffer().Materialize());

		const auto entry = m_Lenses.find(item.Path);

		if (entry == m_Lenses.end())
		{
			return;
		}

		LensState& state = entry->second;

		// Replaced wholesale, not edited: the declarations in hand describe text that need not
		// resemble what is there now, so they are dropped outright rather than shifted the way an
		// edit's stopgap shifts them — a lens row left over from the old text would be a count
		// drawn confidently above whatever happens to be on that line now. Asked for again at
		// once rather than on the retry clock.
		state.Declarations.clear();
		state.Model.SetDeclarations(state.Declarations, item.Rows, item.Document->GetBuffer(),
									item.TabWidth);
		state.HaveDeclarations = false;
		state.WantSymbols = true;
		state.NextSymbolsAt = 0.0;

		DropInFlightCounts(item.Path, state);
	}

	void DocumentWindows::AskForSymbols(const Core::OpenItem& item, LensState& state)
	{
		const double now = ImGui::GetTime();

		if (!state.WantSymbols || state.SymbolsOutstanding || now < state.NextSymbolsAt)
		{
			return;
		}

		const LanguageServices::Ticket ticket =
			m_LanguageServices.AskSymbols(item.Path, LanguageOfPath(item.Path));

		m_PendingSymbols.emplace(ticket, item.Path);
		state.SymbolsOutstanding = true;
		state.NextSymbolsAt = now + kSymbolsRetrySeconds;
	}

	void DocumentWindows::AskForVisibleCounts(const Core::OpenItem& item, LensState& state,
											  const EditorFrameResult& frame)
	{
		// A screen either side of what was drawn, so scrolling steadily finds the counts already
		// there rather than a blank row for as long as the server takes to answer. Measured from
		// the frame's own span rather than from a constant: that is what "a screen" is here,
		// whatever the window height and font size make it.
		const std::size_t margin = frame.LastVisibleRow - frame.FirstVisibleRow + 1;
		const std::size_t from =
			frame.FirstVisibleRow > margin ? frame.FirstVisibleRow - margin : 0;
		const std::size_t to = frame.LastVisibleRow + margin + 1;

		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		for (const std::size_t row : state.Model.RowsNeedingCounts(from, to))
		{
			// The whole of the re-asking guard: a row already asked about is skipped for as long
			// as it stays in this set, which is until the anchors move. RowsNeedingCounts on its
			// own would return the same rows every frame until the answer arrived, which at sixty
			// frames a second is a request storm at the server for one visible declaration.
			if (!state.Asked.insert(row).second)
			{
				continue;
			}

			const Lens* lens = state.Model.LensAt(row);

			if (lens == nullptr)
			{
				continue;
			}

			const Core::RowRef ref = item.Rows.RowAt(row);
			const std::string lineText = buffer.LineText(ref.Line);

			FilePosition where;
			where.Path = item.Path;
			where.Line = ref.Line;
			// The lens carries a VISUAL column, in cells, because that is what draws it in the
			// right place. The protocol wants UTF-16 code units, so it goes back through the
			// line's own bytes: cells to a byte offset with the tab width, and the byte offset to
			// a Utf16Column with the line's text. Neither step can be skipped on a line holding a
			// tab or a character outside ASCII.
			where.Column = ToUtf16Column(
				lineText, Core::ByteOfColumn(lineText, lens->Column, item.TabWidth));

			const LanguageServices::Ticket ticket =
				m_LanguageServices.AskReferences(item.Path, LanguageOfPath(item.Path), where);

			m_PendingCounts.emplace(ticket, PendingCount{item.Path, row});
		}
	}

	void DocumentWindows::UpdateLenses(const Core::OpenItem& item, LensState& state,
									   const EditorFrameResult& frame)
	{
		if (frame.Edited)
		{
			// Once per frame per document, not once per edit: the widget reports that this frame
			// changed the buffer, however many edits went into it, and a transaction touching
			// twenty lines would otherwise send the whole file twenty times in one frame. The
			// server only needs the state the frame ended in.
			m_LanguageServices.ChangeDocument(item.Path, LanguageOfPath(item.Path),
											  item.Document->GetBuffer().Materialize());

			// The declarations the server last reported are now about text that has moved. The
			// model has already shifted its anchors as a stopgap; what is asked for here is the
			// exact set, at once rather than on the retry clock.
			state.WantSymbols = true;
			state.NextSymbolsAt = 0.0;

			DropInFlightCounts(item.Path, state);
		}

		// A lens anchors to a row, and every row moved when the wrap width did — so the anchors
		// are rebuilt from the declarations already in hand rather than waiting for the server to
		// say the same thing again. SetDeclarations carries the counts over by identity, so
		// nothing is re-asked for.
		if (state.HaveDeclarations && state.BuiltAt != item.WrappedTo)
		{
			state.Model.SetDeclarations(state.Declarations, item.Rows,
										item.Document->GetBuffer(), item.TabWidth);
			state.BuiltAt = item.WrappedTo;
			state.Asked.clear();
		}

		AskForSymbols(item, state);
		AskForVisibleCounts(item, state, frame);
	}

	void DocumentWindows::ApplySymbolAnswer(const std::filesystem::path& path,
											const LanguageServices::Answer& answer)
	{
		const auto entry = m_Lenses.find(path);

		if (entry == m_Lenses.end())
		{
			return;
		}

		LensState& state = entry->second;

		state.SymbolsOutstanding = false;

		// A failure says nothing about whether the next attempt would succeed — a server that was
		// still indexing answers this way — so WantSymbols is left standing and the retry clock
		// set in AskForSymbols decides when to try again.
		if (answer.Failed)
		{
			return;
		}

		const auto index = m_Documents.Find(path);

		if (!index || !m_Documents.At(*index).Document)
		{
			return;
		}

		const Core::OpenItem& item = m_Documents.At(*index);
		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		// A reply is about the text as the server last saw it, and the buffer may have moved on
		// since — an edit that removed lines from the end leaves declarations naming lines that
		// are no longer there. Both RowMap and TextBuffer clamp rather than fail, so keeping them
		// would not crash; it would anchor a lens past the last line of the file, which is a row
		// drawn where no row belongs. Dropped instead, and the next reply brings them back if the
		// server still says they are there.
		state.Declarations.clear();
		state.Declarations.reserve(answer.Declarations.size());

		for (const Declaration& declaration : answer.Declarations)
		{
			if (declaration.Line < buffer.GetLineCount())
			{
				state.Declarations.push_back(declaration);
			}
		}

		state.HaveDeclarations = true;
		state.WantSymbols = false;
		state.Model.SetDeclarations(state.Declarations, item.Rows, buffer, item.TabWidth);
		state.BuiltAt = item.WrappedTo;

		// The anchors have been replaced outright, so the rows the outstanding requests name are
		// no longer the rows they were asked about. Cleared rather than translated: an answer that
		// arrives for a dropped ticket is ignored, and the counts that survived the rebuild are
		// already known, so only the genuinely unknown rows are asked about again.
		state.Asked.clear();
	}

	void DocumentWindows::RequestDefinition(std::size_t index)
	{
		if (index >= m_Documents.GetCount())
		{
			return;
		}

		Core::OpenItem& item = m_Documents.At(index);

		if (!item.Document)
		{
			return;
		}

		const LanguageId language = LanguageOfPath(item.Path);

		if (ServerConfigFor(language) == nullptr)
		{
			// Nothing configured to ask - the same answer a server that never started would give,
			// said immediately rather than after a round trip that could not have happened.
			SetTransientMessage("no definition found");
			return;
		}

		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		const Core::Position position = buffer.ToPosition(item.Caret.Offset);
		const std::string lineText = buffer.LineText(position.Line);

		FilePosition where;
		where.Path = item.Path;
		where.Line = position.Line;
		where.Column = ToUtf16Column(lineText, position.Column);

		// A second F12 before the first has answered supersedes it: the user is asking a new
		// question, and an answer to the old one arriving late would be about a place they have
		// since moved past.
		if (m_PendingDefinition)
		{
			m_LanguageServices.Cancel(m_PendingDefinition->Ticket);
		}

		m_LanguageServices.EnsureStarted(item.Path, language);

		const LanguageServices::Ticket ticket =
			m_LanguageServices.AskDefinition(item.Path, language, where);

		// Whatever this document's own last draw left in m_CaretScreen — default-constructed,
		// screen position (0, 0), on the vanishingly unlikely frame nothing has drawn it yet.
		const auto screen = m_CaretScreen.find(item.Path);
		const CaretScreenPosition screenPosition =
			screen != m_CaretScreen.end() ? screen->second : CaretScreenPosition{};

		m_PendingDefinition =
			PendingDefinition{ticket, item.Path, item.Caret.Offset, where, screenPosition};
	}

	void DocumentWindows::RequestReferences(std::size_t index, ReferencesPanel& panel)
	{
		if (index >= m_Documents.GetCount())
		{
			return;
		}

		Core::OpenItem& item = m_Documents.At(index);

		if (!item.Document)
		{
			return;
		}

		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		const Core::Position position = buffer.ToPosition(item.Caret.Offset);
		const std::string lineText = buffer.LineText(position.Line);

		FilePosition where;
		where.Path = item.Path;
		where.Line = position.Line;
		where.Column = ToUtf16Column(lineText, position.Column);

		// The word touching the caret, the same word a double click would select - Shift+F12 has
		// nothing else to name the symbol with while the reply is still outstanding.
		const Core::Caret word = Core::SelectWordAt(buffer, item.Caret.Offset);
		const auto [wordFrom, wordTo] = Core::SelectionRange(word);
		const std::string symbol = buffer.Substring(wordFrom, wordTo);

		RequestReferencesAt(item, LanguageOfPath(item.Path), where, symbol, panel);
	}

	void DocumentWindows::RequestReferencesForLens(const Core::OpenItem& item,
												   const LensState& state, std::size_t documentRow,
												   ReferencesPanel& panel)
	{
		for (const Declaration& declaration : state.Declarations)
		{
			if (item.Rows.FirstRowOf(declaration.Line) != documentRow)
			{
				continue;
			}

			FilePosition where;
			where.Path = item.Path;
			where.Line = declaration.Line;
			where.Column = declaration.Column;

			RequestReferencesAt(item, LanguageOfPath(item.Path), where, declaration.Name, panel);
			return;
		}
	}

	void DocumentWindows::RequestReferencesAt(const Core::OpenItem& item, LanguageId language,
											  const FilePosition& where, const std::string& symbol,
											  ReferencesPanel& panel)
	{
		if (ServerConfigFor(language) == nullptr)
		{
			// Nothing configured to ask - the same answer a server that never started would give,
			// said immediately rather than after a round trip that could not have happened.
			SetTransientMessage("no references found");
			return;
		}

		// A second question before the first has answered supersedes it, on the same reasoning as
		// RequestDefinition: the user is asking about something else now, and a late answer to the
		// old question would be about a symbol they have since moved past.
		if (m_PendingReferences)
		{
			m_LanguageServices.Cancel(m_PendingReferences->Ticket);
		}

		m_LanguageServices.EnsureStarted(item.Path, language);

		const LanguageServices::Ticket ticket =
			m_LanguageServices.AskReferences(item.Path, language, where);

		m_PendingReferences = PendingReferences{ticket, symbol};

		panel.ShowWorking(symbol);
		panel.Focus();
	}

	void DocumentWindows::ApplyDefinitionAnswer(const PendingDefinition& pending,
												const LanguageServices::Answer& answer)
	{
		// The stand-in generation check: a document closed, or a caret that has moved since the
		// request, both mean this answer is about a question that no longer stands.
		const auto index = m_Documents.Find(pending.Path);

		if (!index || m_Documents.At(*index).Caret.Offset != pending.CaretOffset)
		{
			return;
		}

		if (answer.Failed)
		{
			SetTransientMessage("no definition found");
			return;
		}

		std::vector<Location> candidates;
		const DefinitionOutcome outcome = ResolveDefinition(answer.Locations, pending.From, candidates);

		switch (outcome)
		{
		case DefinitionOutcome::None:
			SetTransientMessage("no definition found");
			break;

		case DefinitionOutcome::Jumped:
			if (!candidates.empty())
			{
				Jump(m_Documents, candidates.front());
			}
			break;

		case DefinitionOutcome::NeedsChoice:
			m_DefinitionChoices = std::move(candidates);
			// At the caret, not the mouse: a keyboard-triggered F12 never touches the pointer,
			// which could be anywhere — another panel, another monitor — so a popup that opened
			// there would read as landing nowhere near the code being looked at. Ctrl+click's own
			// caret sits under the pointer anyway, so one rule serves both gestures.
			ImGui::SetNextWindowPos(ImVec2(pending.Screen.X, pending.Screen.Y));
			// Called here, before any window this frame has opened, rather than deferred to
			// RenderDefinitionChoices: CollectServerAnswers runs at the very top of Render, ahead
			// of every Begin, which is exactly where OpenPopup is safe to call.
			ImGui::OpenPopup(kDefinitionChoicesTitle);
			break;
		}
	}

	void DocumentWindows::RenderDefinitionChoices(const ResolvedScheme& scheme,
												  std::size_t tabWidth)
	{
		if (!ImGui::BeginPopup(kDefinitionChoicesTitle))
		{
			return;
		}

		const EditorPalette editorPalette = ReadEditorPalette(scheme);
		const SyntaxPalette syntaxPalette = ReadSyntaxPalette(scheme);

		m_DefinitionPreview.SetTabWidth(tabWidth);

		for (std::size_t index = 0; index < m_DefinitionChoices.size(); ++index)
		{
			const Location& location = m_DefinitionChoices[index];

			ImGui::PushID(static_cast<int>(index));

			const std::string header = Core::ToUtf8(location.Path.filename()) + ":" +
									   std::to_string(location.Line + 1);

			if (ImGui::Selectable(header.c_str()))
			{
				Jump(m_Documents, location);
				ImGui::CloseCurrentPopup();
			}

			// The line the location names, coloured the way the editor would draw it -
			// PreviewContent already knows how to lex and colour one line without this owning a
			// whole SyntaxCache per candidate.
			m_DefinitionPreview.Show(location.Path, m_Documents);

			if (const Core::TextBuffer* buffer = m_DefinitionPreview.GetBuffer();
				buffer != nullptr && location.Line < buffer->GetLineCount())
			{
				const std::string expanded =
					Core::ExpandTabs(buffer->LineText(location.Line), tabWidth);
				const std::vector<Token>& tokens = m_DefinitionPreview.TokensOf(location.Line);

				ImGui::Indent();

				std::size_t drawn = 0;
				bool first = true;

				const auto drawRun = [&](std::size_t from, std::size_t to, ImU32 colour)
				{
					if (to <= from || from >= expanded.size())
					{
						return;
					}

					if (!first)
					{
						ImGui::SameLine(0.0f, 0.0f);
					}

					first = false;

					const std::size_t end = std::min(to, expanded.size());
					const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(colour);

					ImGui::TextColored(rgba, "%.*s", static_cast<int>(end - from),
									   expanded.data() + from);
				};

				if (tokens.empty())
				{
					drawRun(0, expanded.size(), editorPalette.Text);
				}
				else
				{
					for (const Token& token : tokens)
					{
						drawRun(drawn, token.From, editorPalette.Text);
						drawRun(token.From, token.To, syntaxPalette.Of(token.Kind));
						drawn = token.To;
					}

					drawRun(drawn, expanded.size(), editorPalette.Text);
				}

				ImGui::Unindent();
			}

			ImGui::PopID();
		}

		ImGui::EndPopup();
	}

	void DocumentWindows::CollectServerAnswers(ReferencesPanel& referencesPanel)
	{
		std::vector<LanguageServices::Answer> answers;

		m_LanguageServices.TakeAnswers(answers);

		for (const LanguageServices::Answer& answer : answers)
		{
			if (m_PendingDefinition && m_PendingDefinition->Ticket == answer.For)
			{
				const PendingDefinition pending = *m_PendingDefinition;

				m_PendingDefinition.reset();
				ApplyDefinitionAnswer(pending, answer);
				continue;
			}

			if (m_PendingReferences && m_PendingReferences->Ticket == answer.For)
			{
				const PendingReferences pending = *m_PendingReferences;

				m_PendingReferences.reset();

				// A failed request - no server, or one that never got a process - reads the same
				// as a reply naming nothing: ShowResults with an empty vector says "no references
				// found" either way, rather than leaving the panel stuck showing "working".
				referencesPanel.ShowResults(pending.Symbol,
											answer.Failed ? std::vector<Location>{}
														 : answer.Locations);
				continue;
			}

			if (const auto symbols = m_PendingSymbols.find(answer.For);
				symbols != m_PendingSymbols.end())
			{
				const std::filesystem::path path = symbols->second;

				m_PendingSymbols.erase(symbols);
				ApplySymbolAnswer(path, answer);
				continue;
			}

			const auto count = m_PendingCounts.find(answer.For);

			if (count == m_PendingCounts.end())
			{
				// Nothing here asked this, or what asked it has since been dropped — an edit
				// invalidated the row, or the document closed. Either way the answer is about a
				// question that no longer stands.
				continue;
			}

			const PendingCount pending = count->second;

			m_PendingCounts.erase(count);

			if (answer.Failed)
			{
				// Left in state.Asked deliberately: a failure that is re-asked the moment it comes
				// back is a request per frame for as long as the server keeps failing. The next
				// edit or reply clears the set and gives the row another chance.
				continue;
			}

			if (const auto entry = m_Lenses.find(pending.Path); entry != m_Lenses.end())
			{
				entry->second.Model.SetCount(pending.DocumentRow, answer.Locations.size());
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
		unsigned int fallbackDockNode, bool wordWrap, const ResolvedScheme& scheme,
		const std::optional<std::filesystem::path>& focusRequest, ReferencesPanel& referencesPanel)
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

				// Null for a document no server serves, which is what spares the widget a branch
				// on whether one exists — the same role an empty token list plays for SyntaxCache.
				LensState* const lens = LensFor(item);

				EditorView view;
				view.FontSize = fontSize;
				view.Indent = indentStyle;
				view.WordWrap = wordWrap;
				view.Syntax = &SyntaxFor(item);
				view.Scheme = &scheme;
				view.Lens = lens != nullptr ? &lens->Model : nullptr;

				const EditorFrameResult editorResult = DrawEditor(context, item, view, m_FindBar);

				// Recorded every frame the document draws, not only when something asks: a
				// keyboard-triggered request reads this back later, from whichever frame drew
				// last, so it has to already be here rather than computed on demand.
				m_CaretScreen[item.Path] =
					CaretScreenPosition{editorResult.CaretScreenX, editorResult.CaretScreenY};

				// After the draw, not before: what to ask the server for depends on the rows the
				// frame actually drew, and whether to tell it about an edit depends on whether one
				// happened during it.
				if (lens != nullptr)
				{
					UpdateLenses(item, *lens, editorResult);
				}

				// Once the user has typed into a preview it is no longer something they were
				// just glancing at.
				if (editorResult.Edited && preview)
					m_Documents.Promote(index);

				if (editorResult.SaveRequested)
					RequestSave(index);

				// Ctrl+click asks the same question F12 does, from wherever the click just put
				// the caret — reported through the frame result rather than resolved from the
				// active document, so the request is unambiguously about the document that was
				// actually clicked even on the one frame a click both switches and asks.
				if (editorResult.DefinitionRequested)
					RequestDefinition(index);

				// A lens row holds no text, so the click cannot become a caret position — it is
				// reported as the document row the lens anchors to instead, and resolved back to
				// the declaration through this document's own lens state.
				if (lens != nullptr && editorResult.LensClicked)
					RequestReferencesForLens(item, *lens, *editorResult.LensClicked, referencesPanel);
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
								 unsigned int fallbackDockNode, bool wordWrap,
								 const ResolvedScheme& scheme, ReferencesPanel& referencesPanel)
	{
		const std::vector<std::string> startingIdentities = CurrentIdentities(m_Documents);

		// Before the documents draw, so an answer that arrived since the last frame is on screen
		// this one. GraphiteLayer has already called LanguageServices::Poll, which is what moved
		// the servers' replies into the queue this drains.
		CollectServerAnswers(referencesPanel);

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
		RenderDefinitionChoices(scheme, indentStyle.Width);

		RenderDocumentWindows(context, fontSize, indentStyle, fallbackDockNode, wordWrap, scheme,
							  focusRequest, referencesPanel);

		// Once per frame, not once per document: a prune that ran inside the per-document loop
		// above would cost a walk of m_Syntax for every open document instead of one for the
		// whole frame.
		PruneSyntaxCaches();
		PruneLensStates();

		// After the loop, not before: a renamed document's pending dock target has to be
		// consumed by Begin() above before its old identity's node can be undocked and
		// possibly deleted here, or the renamed document is stranded in a fresh orphan node.
		ForgetLayoutOfWindowsClosedElsewhere(startingIdentities);

		m_PreviousIdentities = CurrentIdentities(m_Documents);
	}
}

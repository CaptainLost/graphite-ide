#include "Editor/DocumentWindows.h"

#include "Editor/DefinitionNavigation.h"
#include "Editor/Palettes.h"
#include "Editor/TextEditorWidget.h"

#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerRegistry.h"

#include "Persistence/CaretRestore.h"

#include "UI/RememberedPlace.h"

#include "Core/Base/Log.h"
#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/Platform/Shell.h"
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
#include <system_error>
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
		// Whether this opened a tab or landed on one already there, the item now carries
		// OpenItem::PendingFocus and the render pass below is what spends it.
		const Core::OpenOutcome outcome = m_Documents.Open(path, permanent);

		// Opened from inside the folder that is open now, so it belongs to that folder, even if
		// its tab was carried over from the one before.
		if (const auto state = m_States.find(path); state != m_States.end())
		{
			state->second.CarriedOver = false;
		}

		if (outcome != Core::OpenOutcome::NeedsConfirmation)
			return;

		AskAboutSize(path, permanent);
	}

	WorkspaceState DocumentWindows::CaptureState() const
	{
		WorkspaceState state;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (!item.Document)
			{
				continue;
			}

			// Still open from the folder this one replaced: its tabs were written to that folder,
			// and recording them here too would mix two folders' tabs into one.
			const auto found = m_States.find(item.Path);
			const DocumentState* const kept = found != m_States.end() ? &found->second : nullptr;

			if (kept != nullptr && kept->CarriedOver)
			{
				continue;
			}

			const Core::Position caret = item.Document->GetBuffer().ToPosition(item.Caret.Offset);

			TabState tab;
			tab.Path = item.Path;
			tab.Preview = item.Preview;
			tab.Line = caret.Line;
			tab.Column = caret.Column;

			// A view still on its way to a restored line is reported as going there: the anchor
			// says where its first frames happened to land, not where the reader was.
			if (kept != nullptr && kept->PendingTop)
			{
				tab.Top = kept->PendingTop->Line;
			}
			else if (kept != nullptr && kept->Anchor.Taken)
			{
				tab.Top = kept->Anchor.Line;
			}

			state.Tabs.push_back(std::move(tab));
		}

		if (const auto active = m_Documents.GetActive();
			active && m_Documents.At(*active).Document && !IsCarriedOver(m_Documents.At(*active).Path))
		{
			state.Active = m_Documents.At(*active).Path;
		}

		return state;
	}

	void DocumentWindows::CarryOverOpenDocuments()
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			StateFor(m_Documents.At(index).Path).CarriedOver = true;
		}
	}

	void DocumentWindows::RestoreState(const WorkspaceState& state)
	{
		for (const TabState& tab : state.Tabs)
		{
			// The new folder's own tabs name this file, so it belongs to this folder as well, and
			// the tab it already has — carried over or not — is recorded here from now on.
			if (const auto kept = m_States.find(tab.Path); kept != m_States.end())
			{
				kept->second.CarriedOver = false;
			}

			std::error_code error;

			if (!std::filesystem::is_regular_file(tab.Path, error))
			{
				APP_INFO("Not reopening '{}': it is no longer there", Core::ToUtf8(tab.Path));
				continue;
			}

			// Already open — a folder dropped over another can share files with it — and the
			// tab someone is using keeps its own caret and view.
			if (m_Documents.Find(tab.Path))
			{
				continue;
			}

			// Confirmed: a large file's size question was answered in the launch it was opened
			// in, and asking again every launch would be a question about nothing.
			LoadConfirmed(tab.Path, !tab.Preview);

			const auto index = m_Documents.Find(tab.Path);

			if (!index)
			{
				continue;
			}

			Core::OpenItem& item = m_Documents.At(*index);

			// Only the active tab is brought forward, below; every open marked its own.
			item.PendingFocus = false;
			m_RestoredIdentities.insert(Identity(m_Documents, *index));

			if (!item.Document)
			{
				continue;
			}

			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const std::size_t offset = RestoredCaretOffset(buffer, tab.Line, tab.Column);

			item.Caret = Core::Caret{.Offset = offset, .Anchor = offset};

			if (const std::size_t top = RestoredTopLine(buffer, tab.Top); top > 0)
			{
				StateFor(tab.Path).PendingTop = PendingTopLine{.Line = top, .CaretOffset = offset};
			}
		}

		if (state.Active)
		{
			if (const auto index = m_Documents.Find(*state.Active))
			{
				// Active, so commands act on it, but not brought forward: which tab of each node
				// is in front is what imgui.ini remembers, and the reader may have left another
				// kind of window - a panel, a session - in front of it.
				m_Documents.Activate(*index);
			}
		}
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

		// Moved the same way RewritePaths moved the tabs, so a carried-over tab under the moved
		// directory is still recognised as carried over under its new path.
		//
		// Collected before any is moved, so a path moved onto cannot be mistaken for one still to
		// move. The state left under the old path goes with the next prune, like the rest of it.
		std::vector<std::filesystem::path> carriedOver;

		for (auto& [path, state] : m_States)
		{
			if (state.CarriedOver && Core::RelativeTo(path, from))
			{
				carriedOver.push_back(path);
				state.CarriedOver = false;
			}
		}

		for (const std::filesystem::path& path : carriedOver)
		{
			const auto relative = Core::RelativeTo(path, from);

			StateFor(*relative == "." ? to : to / *relative).CarriedOver = true;
		}

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
		ReloadBatch batch;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			// The watcher names directories, so an item is worth a stat only when its own
			// directory is one of them. `everything` means the batch could not be narrowed
			// down at all — dropped notifications, or a git state change.
			if (!everything && !LiesInAnyOf(directories, m_Documents.At(index).Path))
			{
				continue;
			}

			RespondToSync(index, Core::SyncWithFile(m_Documents, index), batch);
		}

		AnnounceExpectedReloads(batch);
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
		ReloadBatch batch;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			RespondToSync(index, Core::SyncWithFile(m_Documents, index), batch);
		}

		AnnounceExpectedReloads(batch);
	}

	void DocumentWindows::AnnounceExpectedReloads(const ReloadBatch& batch)
	{
		// A failure has already said "Could not reload" and why, which matters more than how
		// many others went well; the batch's line would write over it.
		if (batch.AnyFailed)
		{
			return;
		}

		const std::vector<std::size_t>& reloaded = batch.Expected;

		// Said once for the batch: a checkout rewrites several open files in one go, and the
		// application asked for every one of those writes, so "changed on disk" - the words
		// for somebody else's change - would be wrong about each of them.
		if (reloaded.size() == 1)
		{
			SetTransientMessage(Core::ToUtf8(m_Documents.At(reloaded.front()).Path.filename()) +
								" reloaded (written by git)");
		}
		else if (reloaded.size() > 1)
		{
			SetTransientMessage("Reloaded " + std::to_string(reloaded.size()) + " files written by git");
		}
	}

	void DocumentWindows::ReloadExpected(std::size_t index, ReloadBatch& batch)
	{
		if (ReloadOne(index, false))
		{
			batch.Expected.push_back(index);
		}
		else
		{
			batch.AnyFailed = true;
		}
	}

	void DocumentWindows::RespondToSync(std::size_t index, Core::FileSyncOutcome outcome, ReloadBatch& batch)
	{
		switch (outcome)
		{
		case Core::FileSyncOutcome::NeedsDecision:
		{
			const std::filesystem::path path = m_Documents.At(index).Path;

			// A change this application made itself, which the user has already been asked
			// about once, by whatever asked for the write.
			if (m_ExpectedWrites.TakeFor(path))
			{
				ReloadExpected(index, batch);
				break;
			}

			// Asked rather than reloaded, and queued, so several files changing in one batch
			// (a git checkout) are asked about one at a time instead of every modal fighting
			// to open this frame.
			if (!m_ReloadConflict.Asked(path))
			{
				m_ReloadConflict.Ask(path);
			}

			break;
		}

		case Core::FileSyncOutcome::SafeToReload:
			// Taken here too, though a clean tab would be reloaded either way: what differs is
			// the message, and a checkout's writes reach most tabs through this case.
			if (m_ExpectedWrites.TakeFor(m_Documents.At(index).Path))
			{
				ReloadExpected(index, batch);
				break;
			}

			if (!ReloadOne(index))
			{
				batch.AnyFailed = true;
			}

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

	std::optional<std::size_t> DocumentWindows::FindOpen(const std::filesystem::path& path) const
	{
		const std::filesystem::path wanted = Core::NormalizePath(path);

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			if (Core::SamePath(Core::NormalizePath(m_Documents.At(index).Path), wanted))
			{
				return index;
			}
		}

		return std::nullopt;
	}

	bool DocumentWindows::ReloadOne(std::size_t index, bool announce)
	{
		const std::string fileName = Core::ToUtf8(m_Documents.At(index).Path.filename());

		if (const auto result = Core::ReloadItem(m_Documents, index))
		{
			AnnounceReload(m_Documents.At(index));

			if (announce)
			{
				SetTransientMessage(fileName + " reloaded (changed on disk)");
			}

			return true;
		}
		else
		{
			SetTransientMessage("Could not reload " + fileName + ": " + result.GetError().Message);
			return false;
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
			m_Server.SaveDocument(item.Path, LanguageOfPath(item.Path));

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

	void DocumentWindows::DrawFileTabItems(std::size_t index, const std::filesystem::path& workspaceRoot)
	{
		const Core::OpenItem& item = m_Documents.At(index);

		// Enabled only when there is something to write: a document with no unsaved changes, or
		// a tab whose open failed, has nothing the file on disk does not already hold.
		const bool unsaved = item.Document && item.Document->IsModified();

		if (ImGui::MenuItem("Save", nullptr, false, unsaved))
			RequestSave(index);

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Path"))
			ImGui::SetClipboardText(Core::ToUtf8(item.Path).c_str());

		if (ImGui::MenuItem("Copy Relative Path"))
		{
			std::error_code code;
			const std::filesystem::path relative =
				workspaceRoot.empty() ? std::filesystem::path{}
									  : std::filesystem::relative(item.Path, workspaceRoot, code);

			// With no folder open, or when the two share no prefix, the absolute path is
			// still worth copying.
			const bool usable = !workspaceRoot.empty() && !code && !relative.empty();
			ImGui::SetClipboardText(Core::ToUtf8(usable ? relative : item.Path).c_str());
		}

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const auto revealed = Core::Shell::RevealInFileBrowser(item.Path); !revealed)
				APP_ERROR("{}", revealed.GetError().Message);
		}
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

	bool DocumentWindows::SaveDocuments(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			if (const auto index = FindOpen(path); index && !SaveOne(*index))
			{
				return false;
			}
		}

		return true;
	}

	void DocumentWindows::RevertDocuments(const std::vector<std::filesystem::path>& paths)
	{
		std::size_t reverted = 0;
		bool failed = false;

		for (const std::filesystem::path& path : paths)
		{
			// ReloadFromDisk replaces the buffer and marks it saved whether or not it was modified,
			// so this is the same reload a disk change gets, with no question in front of it.
			if (const auto index = FindOpen(path))
			{
				if (ReloadOne(*index, false))
				{
					++reverted;
				}
				else
				{
					failed = true;
				}
			}
		}

		// A question about a change on disk, queued before the revert, is about a document that
		// is now clean and matches the disk: answering it would reload nothing or, for "keep
		// mine", accept a state that is no longer there. m_ExpectedWrites is left alone: an
		// entry in it is for a write still to come, which the revert has not made.
		m_ReloadConflict.WithdrawIf([this, &paths](const std::filesystem::path& queued) {
			return std::any_of(paths.begin(), paths.end(), [&queued](const std::filesystem::path& path) {
				return Core::SamePath(Core::NormalizePath(path), Core::NormalizePath(queued));
			});
		});

		// A failed reload has already said so, and that is the message worth keeping.
		if (!failed && reverted > 0)
		{
			SetTransientMessage("Discarded unsaved changes in " + std::to_string(reverted) +
								(reverted == 1 ? " file" : " files"));
		}
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
		m_RestoredIdentities.erase(Identity(m_Documents, index));
		m_Documents.Close(index);
		// After the close, so the prune sees the document gone and tells the server so. Erasing
		// here directly would be the same thing minus the CloseDocument the prune sends.
		PruneDocumentStates();
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

		// Withdrawn while its popup was open (the document was reverted): nothing left to ask.
		if (path == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_ReloadConflict.End();
			return;
		}

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

	DocumentWindows::DocumentState& DocumentWindows::StateFor(const std::filesystem::path& path)
	{
		return m_States[path];
	}

	DocumentWindows::LensState* DocumentWindows::FindLens(const std::filesystem::path& path)
	{
		const auto state = m_States.find(path);

		return state != m_States.end() && state->second.Lens ? &*state->second.Lens : nullptr;
	}

	bool DocumentWindows::IsCarriedOver(const std::filesystem::path& path) const
	{
		const auto state = m_States.find(path);

		return state != m_States.end() && state->second.CarriedOver;
	}

	SyntaxCache& DocumentWindows::SyntaxFor(const Core::OpenItem& item)
	{
		std::optional<SyntaxCache>& syntax = StateFor(item.Path).Syntax;

		if (!syntax)
		{
			syntax.emplace();
			syntax->SetLexer(LexerFor(LanguageOfPath(item.Path)));
			// Keeps the cache's width equal to the item's when a fresh cache is made for an item
			// that was already at a non-default width — the case being a path rewrite while the
			// document stays open: RewritePaths changes item.Path (a rename of the file or of a
			// directory it lives under), the state under the new key starts with no cache, and
			// without this seed the new cache would sit at Core::DefaultTabWidth forever.
			// DrawEditor's compare-and-rebuild block in TextEditorWidget.cpp only fires on
			// item.TabWidth != view.Indent.Width, which is already false the moment the item's own
			// width was set correctly earlier — so nothing would ever re-lex the cache at the right
			// width.
			syntax->SetTabWidth(item.TabWidth);
		}

		return *syntax;
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

		DocumentState& state = StateFor(item.Path);

		if (!state.Lens)
		{
			state.Lens.emplace();

			// EnsureStarted before OpenDocument, in that order: OpenDocument is a no-op while no
			// server runs for this root, and every answer afterwards would be about a file the
			// server was never told exists. Both are safe to repeat, but this is the one moment
			// per document they are needed.
			m_Server.EnsureStarted(item.Path, language);
			m_Server.OpenDocument(item.Path, language, item.Document->GetBuffer().Materialize());

			// Its declarations and colours are wanted from now on.
			m_Router.Track(item.Path);

			// After the server is told, not before: this only fills the gap until the server
			// answers, and the answer has to already be on its way for the gap to be a gap.
			RestoreAnalysis(item, state);
		}

		return &*state.Lens;
	}

	// Drops every entry whose key is not the path of a currently open document. Run once a frame
	// rather than at the one place a path changes: m_States is a side-map keyed on an identity
	// DocumentSet owns and can rewrite without this map hearing about it — see m_States. A
	// closed tab goes the same way, from CloseAndForgetLayout.
	//
	// The server is told the document closed from here as well, so its copy does not outlive the
	// tab: a path that has gone from DocumentSet is one nothing will ask about again.
	void DocumentWindows::PruneDocumentStates()
	{
		for (auto it = m_States.begin(); it != m_States.end();)
		{
			if (m_Documents.Find(it->first))
			{
				++it;
				continue;
			}

			if (const std::optional<LensState>& lens = it->second.Lens)
			{
				m_Server.CloseDocument(it->first, LanguageOfPath(it->first));

				// Kept rather than dropped, so reopening this document paints in the frame it
				// opens. The server is still told it closed: this remembers the answers, not the
				// document, and holding the document open with the server would make it keep a
				// full parse of every file ever glanced at.
				//
				// Before the entry is erased, never after: the colours it keeps are this entry's.
				KeepAnalysis(it->first, *lens, it->second.Semantic);
				m_Router.Forget(it->first);
			}

			it = m_States.erase(it);
		}

		// The same rule: a closed tab's markers, and anything still being computed for it, are
		// dropped.
		m_ChangeMarkers.Retain(
			[this](const std::filesystem::path& path) { return m_Documents.Find(path).has_value(); });
	}

	bool DocumentWindows::IsAnalysing(const std::filesystem::path& path) const
	{
		return m_Router.IsAnalysing(path);
	}

	void DocumentWindows::KeepAnalysis(const std::filesystem::path& path, const LensState& lens,
									   const SemanticOverlay& semantic)
	{
		CachedAnalysis analysis;
		analysis.Declarations = lens.Declarations;
		analysis.Counts = lens.Model.KnownCounts();
		analysis.Semantic = semantic;

		std::optional<CachedAnalysis> worth =
			AnalysisWorthKeeping(std::move(analysis), lens.WasModified, ModifiedTimeOf(path));

		if (worth)
		{
			// Both tiers, exactly as the background pass writes both: what this tab learned is
			// worth as much to the next session as to this one, and a store filled only by the
			// pass would forget every file a reader opened before the walk reached it.
			//
			// Unguarded by the workspace-analysis setting, and deliberately: a store detached
			// because that setting is off — or because no folder is open — drops the write itself
			// rather than creating a directory for it, so a guard here would restate what
			// AnalysisStore::Write already promises and give a second place for the two to drift.
			m_AnalysisStore.Write(path, *worth);

			m_Analysis.Put(path, std::move(*worth));
		}
	}

	void DocumentWindows::RestoreAnalysis(const Core::OpenItem& item, DocumentState& state)
	{
		const std::optional<std::filesystem::file_time_type> modified = ModifiedTimeOf(item.Path);

		if (!modified || !item.Document)
		{
			return;
		}

		std::optional<CachedAnalysis> restored = m_Analysis.Take(item.Path, *modified);

		if (!restored)
		{
			// The memory cache holds this session's work; the store holds every session before it.
			// Asked second because a memory hit is both fresher and free — and this read touches
			// the disk in the frame the tab opens, which AnalysisStore::Read argues for at length.
			restored = m_AnalysisStore.Read(item.Path, *modified);
		}

		if (!restored)
		{
			return;
		}

		LensState& lens = *state.Lens;

		lens.Declarations = std::move(restored->Declarations);
		lens.HaveDeclarations = !lens.Declarations.empty();
		lens.Model.SeedCounts(std::move(restored->Counts));

		// Asked for rather than done here, and this is the whole subtlety of restoring: a lens
		// anchors to a ROW, and this runs before the document has been drawn even once, so its
		// row map is still empty. RowMap::FirstRowOf answers zero for every line of an empty map,
		// which would put every declaration on row zero, collapse them to one under the
		// de-duplication by row, and consume the restored counts against that single survivor.
		// The rebuild a frame later would fix the anchors and have nothing left to anchor.
		//
		// UpdateLenses runs after the editor has drawn, in this same frame, so nothing waits.
		lens.NeedsRebuild = true;

		state.Semantic = std::move(restored->Semantic);

		// The router still wants symbols: this is what the file looked like when its tab closed,
		// and the server is still asked. Its answer replaces all of this, and until it arrives
		// the reader has last time's answer rather than an empty margin.
	}

	std::optional<std::filesystem::file_time_type> DocumentWindows::ModifiedTimeOf(
		const std::filesystem::path& path)
	{
		std::error_code error;
		const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, error);

		// Empty rather than a default stamp: two default stamps compare equal, so a file that
		// could not be read at both ends of a document's life - a share that blinked, a scanner
		// holding it open - would restore an entry nothing had ever checked.
		if (error)
		{
			return std::nullopt;
		}

		return modified;
	}

	SemanticOverlay& DocumentWindows::SemanticFor(const Core::OpenItem& item)
	{
		return StateFor(item.Path).Semantic;
	}

	DiagnosticOverlay& DocumentWindows::DiagnosticFor(const Core::OpenItem& item)
	{
		DocumentState& state = StateFor(item.Path);
		DiagnosticOverlay& overlay = state.Diagnostics;

		// Without a buffer there is nothing to convert positions against, so the overlay is left
		// exactly as it stood - empty for a document that has never had one. Diagnostics are not
		// part of what KeepAnalysis/RestoreAnalysis carry across a close, so there is nothing
		// stale to worry about reusing here either.
		if (!item.Document)
		{
			return overlay;
		}

		const auto& documents = m_Diagnostics.Documents();
		const auto entry = documents.find(item.Path);

		// 0 for a path with no entry at all, which matches a fresh DiagnosticGuard's own default
		// and so never falsely appears unchanged: DiagnosticsModel::Entry::Generation starts at 0
		// too, but only an entry that has never been published to can still read 0 here.
		const std::uint64_t generation = entry != documents.end() ? entry->second.Generation : 0;
		const std::size_t revision = item.Document->GetBuffer().GetRevision();

		DiagnosticGuard& guard = state.DiagnosticsBuiltFrom;

		// Every one of the three inputs to the conversion is unchanged: rebuilding would walk
		// every line a diagnostic touches for no different a result, so the last-built overlay
		// stands.
		if (guard.Revision == revision && guard.TabWidth == item.TabWidth &&
			guard.Generation == generation)
		{
			return overlay;
		}

		guard.Revision = revision;
		guard.TabWidth = item.TabWidth;
		guard.Generation = generation;

		overlay.Set(m_Diagnostics.For(item.Path), item.Document->GetBuffer(), item.TabWidth);

		return overlay;
	}

	void DocumentWindows::DropInFlightCounts(const std::filesystem::path& path, LensState& state)
	{
		// The rows in flight were asked about before the text moved, so their answers are about
		// positions that no longer hold. Dropping the tickets means those answers are ignored when
		// they arrive, and clearing the set lets the rows be asked about again.
		state.Asked.clear();

		m_Router.DropCounts(path);
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
		m_Server.ChangeDocument(item.Path, language, item.Document->GetBuffer().Materialize());

		LensState* const lens = FindLens(item.Path);

		if (lens == nullptr)
		{
			return;
		}

		LensState& state = *lens;

		// Replaced wholesale, not edited: the declarations in hand describe text that need not
		// resemble what is there now, so they are dropped outright rather than shifted the way an
		// edit's stopgap shifts them — a lens row left over from the old text would be a count
		// drawn confidently above whatever happens to be on that line now. Asked for again at
		// once rather than on the retry clock.
		state.Declarations.clear();
		state.Model.SetDeclarations(state.Declarations, item.Rows, item.Document->GetBuffer(),
									item.TabWidth);
		state.HaveDeclarations = false;
		m_Router.WantSymbolsNow(item.Path);

		DropInFlightCounts(item.Path, state);
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

			// The name's own position, in the units the server speaks, carried on the lens since
			// the reply arrived. The drawing column cannot stand in for it: converting cells back
			// to code units lands on where the declaration BEGINS, which for a constructor is its
			// own class's name — so the count would answer about the type rather than about the
			// constructor, and read far too high.
			FilePosition where;
			where.Path = item.Path;
			where.Line = lens->NameLine;
			where.Column = lens->NameColumn;

			m_Router.AskCount(item.Path, row, where);
		}
	}

	void DocumentWindows::UpdateLenses(const Core::OpenItem& item, LensState& state,
									   const EditorFrameResult& frame)
	{
		state.WasModified = item.Document && item.Document->IsModified();

		// A server that has just been replaced failed every request its old client was holding.
		// Those failures leave their rows in Asked, where nothing else would ever take them out,
		// so the counts visible at the moment of the crash would stay blank while every row
		// scrolled into view afterwards got one. The router has already dropped the tickets and
		// asked for everything else again.
		if (m_Router.NoteServerState(item.Path))
		{
			state.Asked.clear();
		}

		if (frame.Edited)
		{
			// Once per frame per document, not once per edit: the widget reports that this frame
			// changed the buffer, however many edits went into it, and a transaction touching
			// twenty lines would otherwise send the whole file twenty times in one frame. The
			// server only needs the state the frame ended in.
			m_Server.ChangeDocument(item.Path, LanguageOfPath(item.Path),
									item.Document->GetBuffer().Materialize());

			// The declarations the server last reported are now about text that has moved. The
			// model has already shifted its anchors as a stopgap; what is asked for here is the
			// exact set, at once rather than on the retry clock.
			m_Router.WantSymbolsNow(item.Path);

			DropInFlightCounts(item.Path, state);
		}

		// A lens anchors to a row, and every row moved when the wrap width did — so the anchors
		// are rebuilt from the declarations already in hand rather than waiting for the server to
		// say the same thing again. SetDeclarations carries the counts over by identity, so
		// nothing is re-asked for.
		if (state.HaveDeclarations && (state.NeedsRebuild || state.BuiltAt != item.WrappedTo))
		{
			state.Model.SetDeclarations(state.Declarations, item.Rows,
										item.Document->GetBuffer(), item.TabWidth);
			state.BuiltAt = item.WrappedTo;
			state.NeedsRebuild = false;
			state.Asked.clear();
		}

		m_Router.AskSymbolsIfDue(item.Path);
		AskForVisibleCounts(item, state, frame);
	}

	bool DocumentWindows::ApplySymbolAnswer(const std::filesystem::path& path,
											const LanguageServices::Answer& answer)
	{
		LensState* const lens = FindLens(path);

		if (lens == nullptr)
		{
			return false;
		}

		LensState& state = *lens;

		const auto index = m_Documents.Find(path);

		if (!index || !m_Documents.At(*index).Document)
		{
			return false;
		}

		const Core::OpenItem& item = m_Documents.At(*index);
		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		// Kept as the server said them, lines past the end of the buffer included: LensModel::
		// SetDeclarations is what drops those, and it does so on every rebuild rather than only
		// on the frame a reply arrives — which is the case that matters, since a rewrap rebuilds
		// against whatever the buffer has become since.
		state.Declarations = answer.Declarations;

		state.HaveDeclarations = true;
		state.Model.SetDeclarations(state.Declarations, item.Rows, buffer, item.TabWidth);
		state.BuiltAt = item.WrappedTo;

		// The anchors have been replaced outright, so the rows the outstanding requests name are
		// no longer the rows they were asked about. Cleared rather than translated: an answer that
		// arrives for a dropped ticket is ignored, and the counts that survived the rebuild are
		// already known, so only the genuinely unknown rows are asked about again.
		state.Asked.clear();

		return true;
	}

	void DocumentWindows::ApplySemanticAnswer(const std::filesystem::path& path,
											  const LanguageServices::Answer& answer)
	{
		const auto index = m_Documents.Find(path);

		if (!index)
		{
			return;
		}

		const Core::OpenItem& item = m_Documents.At(*index);

		if (item.Document)
		{
			StateFor(path).Semantic.Set(answer.SemanticTokens, item.Document->GetBuffer(),
										item.TabWidth);
		}
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

		// Whatever this document's own last draw left in its state — default-constructed, screen
		// position (0, 0), on the vanishingly unlikely frame nothing has drawn it yet.
		const auto state = m_States.find(item.Path);
		const CaretScreenPosition screenPosition =
			state != m_States.end() ? state->second.CaretScreen : CaretScreenPosition{};

		// A second F12 before the first has answered supersedes it; the router cancels the first.
		m_Router.AskDefinition(
			DefinitionQuestion{item.Path, item.Caret.Offset, where, screenPosition});
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

			// The name's position, not the declaration's start. Matched on the declaration's
			// start above, because that is what the lens is anchored to, but asked about by
			// name — a constructor's declaration begins with its own class's name, so asking
			// at the start resolves to the class and counts every mention of the type.
			FilePosition where;
			where.Path = item.Path;
			where.Line = declaration.NameLine;
			where.Column = declaration.NameColumn;

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

		// A second question before the first has answered supersedes it; the router cancels the
		// first.
		m_Router.AskReferences(item.Path, where, symbol);

		panel.ShowWorking(symbol);
		panel.Focus();
	}

	void DocumentWindows::ApplyDefinitionAnswer(const DefinitionQuestion& question,
												const LanguageServices::Answer& answer)
	{
		// The stand-in generation check: a document closed, or a caret that has moved since the
		// request, both mean this answer is about a question that no longer stands.
		const auto index = m_Documents.Find(question.Path);

		if (!index || m_Documents.At(*index).Caret.Offset != question.CaretOffset)
		{
			return;
		}

		if (answer.Failed)
		{
			SetTransientMessage("no definition found");
			return;
		}

		std::vector<Location> candidates;
		const DefinitionOutcome outcome = ResolveDefinition(answer.Locations, question.From, candidates);

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
			// Through the gate, like every other popup this class opens. ImGui cannot stack two
			// popups at level 0: opening this one while "File changed on disk" is up would tear
			// that modal down, and ModalQuestion::WithdrawIfNoLongerOnScreen would then abandon
			// the question rather than ask it again — the reload conflict would never be put to
			// the user at all. Dropping the answer is the cheaper loss of the two: F12 asks
			// again with one keystroke, and the message says why nothing happened.
			if (!m_Modals.TryOpen(kDefinitionChoicesTitle))
			{
				SetTransientMessage("close the open dialog, then press F12 again");
				break;
			}

			m_DefinitionChoices = std::move(candidates);

			// At the caret, not the mouse: a keyboard-triggered F12 never touches the pointer,
			// which could be anywhere — another panel, another monitor — so a popup that opened
			// there would read as landing nowhere near the code being looked at. Ctrl+click's own
			// caret sits under the pointer anyway, so one rule serves both gestures. Kept for
			// RenderDefinitionChoices to spend; see m_DefinitionChoicesAt for why it cannot be
			// spent here.
			m_DefinitionChoicesAt = question.Screen;

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
		// Immediately before the Begin that consumes it, and on the frame the popup appears
		// rather than every frame, so a list the user has scrolled is not dragged back under the
		// caret each frame.
		ImGui::SetNextWindowPos(ImVec2(m_DefinitionChoicesAt.X, m_DefinitionChoicesAt.Y),
								ImGuiCond_Appearing);

		if (!ImGui::BeginPopup(kDefinitionChoicesTitle))
		{
			// ImGui closes a popup on Escape and on a click outside it without telling whatever
			// opened it. The gate would otherwise stay held for the rest of the session and no
			// question anywhere in the application could be asked again — the same guard
			// ModalQuestion keeps for exactly the same reason.
			if (m_Modals.HoldsFor(kDefinitionChoicesTitle) &&
				!ImGui::IsPopupOpen(kDefinitionChoicesTitle))
			{
				m_DefinitionChoices.clear();
				m_Modals.Close(kDefinitionChoicesTitle);
			}

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

	double DocumentWindows::FrameClock()
	{
		return ImGui::GetTime();
	}

	void DocumentWindows::CollectServerAnswers(ReferencesPanel& referencesPanel)
	{
		// What the router hands on, applied to the documents, the panel and the two other askers.
		// Local, because it lives for one call and needs this class's private members.
		class Target final : public LspAnswerTarget
		{
		public:
			Target(DocumentWindows& windows, ReferencesPanel& panel)
				: m_Windows(windows), m_Panel(panel)
			{
			}

			void ApplyDefinition(const DefinitionQuestion& question,
								 const LanguageServices::Answer& answer) override
			{
				m_Windows.ApplyDefinitionAnswer(question, answer);
			}

			void ApplyReferences(const std::string& symbol,
								 const LanguageServices::Answer& answer) override
			{
				// A failed request - no server, or one that never got a process - reads the same
				// as a reply naming nothing: ShowResults with an empty vector says "no references
				// found" either way, rather than leaving the panel stuck showing "working".
				m_Panel.ShowResults(symbol, answer.Failed ? std::vector<Location>{}
														  : answer.Locations);
			}

			void ApplySemanticTokens(const std::filesystem::path& path,
									 const LanguageServices::Answer& answer) override
			{
				m_Windows.ApplySemanticAnswer(path, answer);
			}

			bool ApplySymbols(const std::filesystem::path& path,
							  const LanguageServices::Answer& answer) override
			{
				return m_Windows.ApplySymbolAnswer(path, answer);
			}

			void ApplyCount(const std::filesystem::path& path, std::size_t documentRow,
							std::size_t count) override
			{
				if (LensState* const lens = m_Windows.FindLens(path))
				{
					lens->Model.SetCount(documentRow, count);
				}
			}

			bool Offer(const LanguageServices::Answer& answer) override
			{
				// The preview panes ask for their own colours and the background pass asks about
				// files with no tab at all, each on its own tickets.
				return m_Windows.m_PreviewSemantics.Apply(answer) ||
					   m_Windows.m_WorkspaceAnalysis.Apply(answer);
			}

		private:
			DocumentWindows& m_Windows;
			ReferencesPanel& m_Panel;
		};

		Target target{*this, referencesPanel};

		m_Router.Collect(target);
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
		unsigned int fallbackDockNode, bool wordWrap, ChangeMarkerDisplay changeDisplay,
		const ResolvedScheme& scheme, ReferencesPanel& referencesPanel,
		Core::TabCloseRequests& closeRequests, const std::filesystem::path& workspaceRoot)
	{
		for (std::size_t index = 0; index < m_Documents.GetCount();)
		{
			Core::OpenItem& item = m_Documents.At(index);
			const std::string identity = Identity(m_Documents, index);
			const std::string name = WindowName(m_Documents, index);

			bool restored = false;

			if (IsNewSinceLastFrame(identity))
			{
				// A document reopened from the last launch goes back to where it was — the node it
				// was in, or floating — which imgui.ini already carries; docking it anywhere else
				// here would override that.
				restored = m_RestoredIdentities.erase(identity) > 0;

				if (!restored || !HasRememberedPlace(name))
				{
					if (const unsigned int targetNode = ChooseDockTarget(identity, fallbackDockNode);
						targetNode != 0)
					{
						ImGui::DockBuilderDockWindow(name.c_str(), targetNode);
					}
				}
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
			ImGuiWindowFlags windowFlags =
				modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;

			// ImGui focuses every window on the frame it appears, and focusing a docked window
			// selects its tab — so on the first frame after a launch the last restored window
			// drawn would take the front of its node from whatever imgui.ini remembers there.
			if (restored)
			{
				windowFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
			}

			// Begin() is what turns this into the selected tab of a docked window, which
			// SetWindowFocus called from outside a frame could not do against a window not yet
			// submitted. Taken and cleared in one step, so a focus asked for once is given once
			// and the user's own clicking of another tab is not undone on the next frame.
			if (std::exchange(item.PendingFocus, false))
				ImGui::SetNextWindowFocus();

			bool open = true;
			const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

			// Popped the moment Begin returns, so the dimming reaches this document's tab and
			// title bar without following it into the editor's own contents.
			if (preview)
				ImGui::PopStyleColor();

			// A tab's menu asked for this one last frame: the same as its close button, so an
			// unsaved document is asked about below rather than closed.
			if (closeRequests.Take(name))
				open = false;

			Core::DrawTabContextMenu(closeRequests,
									 [this, index, &workspaceRoot] { DrawFileTabItems(index, workspaceRoot); });

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
				DocumentState& state = StateFor(item.Path);

				EditorView view;
				view.FontSize = fontSize;
				view.Indent = indentStyle;
				view.WordWrap = wordWrap;
				view.ChangeDisplay = changeDisplay;
				view.Syntax = &SyntaxFor(item);
				view.Scheme = &scheme;
				view.Lens = lens != nullptr ? &lens->Model : nullptr;

				// Always handed over, even before a server has said anything: an overlay with
				// nothing in it colours nothing, so the widget needs no branch of its own and a
				// document with no server behind it draws exactly as it always did.
				view.Semantic = &SemanticFor(item);

				// Null when nothing has been said about this document, the same idiom as Lens
				// above: an overlay with nothing in it is indistinguishable from one nobody
				// asked for, so there is no reason to make the widget hold an always-valid but
				// possibly-empty overlay when a null pointer already says the same thing.
				if (DiagnosticOverlay& diagnostics = DiagnosticFor(item); !diagnostics.IsEmpty())
				{
					view.Diagnostics = &diagnostics;
				}

				view.DiagnosticsStale = m_Diagnostics.IsStale(item.Path);

				// A tab whose open failed has no buffer to diff and draws its failure instead;
				// Get below still answers null for it, so the view needs no branch of its own.
				//
				// With both kinds of marker switched off nothing is asked for, so no document is
				// diffed at all. Switching one back on starts over as a first sight would.
				const bool wantsChanges = changeDisplay.Gutter || changeDisplay.Scrollbar;

				if (wantsChanges && item.Document)
				{
					m_ChangeMarkers.Update(item.Path, item.Document->GetBuffer());
				}

				// Kept alive in this scope, since the widget holds only a raw pointer while it draws.
				const std::shared_ptr<const ChangeMarkers> changes =
					wantsChanges ? m_ChangeMarkers.Get(item.Path) : nullptr;
				view.Changes = changes.get();

				// Created on first sight and kept until the tab goes, like the overlays above.
				// The widget fills it in as it draws; until then it is an anchor nobody has taken
				// and the widget skips it.
				view.Anchor = &state.Anchor;
				view.Widget = &state.Widget;

				std::optional<PendingTopLine>& pendingTop = state.PendingTop;

				// A restored line gives way the moment the reader goes somewhere else: navigation
				// into this tab moved the caret or asked for it to be scrolled to, or the wheel
				// turned over it. Seeding again after that would drag the view back to where the
				// last launch left it. The wheel is read here, before the editor draws, because the
				// seed below would otherwise overwrite the scroll it asked for.
				if (pendingTop)
				{
					const bool wheeled = ImGui::GetIO().MouseWheel != 0.0f &&
										 ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

					if (!pendingTop->StillWanted(item.Caret.Offset, item.PendingScroll, wheeled))
					{
						pendingTop.reset();
					}
				}

				if (pendingTop)
				{
					*view.Anchor = AnchorAtLine(pendingTop->Line);
				}

				const EditorFrameResult editorResult = DrawEditor(context, item, view, m_FindBar);

				// Only a frame that drew rows has anything to say: one that drew none left the seed
				// above in the anchor untouched, and Settle would read its own seed as arrival.
				if (pendingTop && editorResult.DrewRows && pendingTop->Settle(*view.Anchor))
				{
					pendingTop.reset();
				}

				// Recorded every frame the document draws, not only when something asks: a
				// keyboard-triggered request reads this back later, from whichever frame drew
				// last, so it has to already be here rather than computed on demand.
				state.CaretScreen =
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

	void DocumentWindows::Render(const CommandContext& context, const EditorFrame& frame,
								 ReferencesPanel& referencesPanel)
	{
		const std::vector<std::string> startingIdentities = CurrentIdentities(m_Documents);

		// Before the documents draw, so an answer that arrived since the last frame is on screen
		// this one. GraphiteLayer has already called LanguageServices::Poll, which is what moved
		// the servers' replies into the queue this drains.
		CollectServerAnswers(referencesPanel);

		// Every question is drawn here, before the per-document loop, because OpenPopup and
		// BeginPopupModal must not run from inside a Begin/BeginChild pair that may not be
		// current when the popup needs to draw. The loop below only ever asks them.
		RenderSizeQuestion();
		RenderReloadConflict();
		RenderOverwriteConfirm();
		RenderCloseConfirm();
		RenderDefinitionChoices(frame.Scheme, frame.Indent.Width);

		RenderDocumentWindows(context, frame.FontSize, frame.Indent, frame.FallbackDockNode,
							  frame.WordWrap, frame.ChangeMarkers, frame.Scheme, referencesPanel,
							  frame.CloseRequests, frame.WorkspaceRoot);

		// Once per frame, not once per document: a prune that ran inside the per-document loop
		// above would cost a walk of m_States for every open document instead of one for the
		// whole frame.
		PruneDocumentStates();

		// After the loop, not before: a renamed document's pending dock target has to be
		// consumed by Begin() above before its old identity's node can be undocked and
		// possibly deleted here, or the renamed document is stranded in a fresh orphan node.
		ForgetLayoutOfWindowsClosedElsewhere(startingIdentities);

		m_PreviousIdentities = CurrentIdentities(m_Documents);
	}
}

#include "Editor/DocumentWindows.h"

#include "Editor/TextEditorWidget.h"

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
			// ImGui shows what precedes ### and identifies by what follows, so the path -
			// the only identity that cannot collide - stays out of sight. That is also what
			// makes the icon free to add: everything before ### is decoration, and no
			// window id, dock position or imgui.ini entry moves because of it.
			//
			// The same IconForExtension the explorer rows use, so one file reads the same
			// in the tree and on its tab. Drawn from the regular mono face, which is the
			// only one carrying the MDI glyphs - nothing here may push another.
			const std::filesystem::path& path = documents.At(index).path;

			return std::string{Core::IconForExtension(Core::ToUtf8(path.extension()))} + "  " + documents.DisplayTitle(index) + "###" + Core::ToUtf8(path);
		}

		std::string Identity(const Core::DocumentSet& documents, std::size_t index)
		{
			return Core::ToUtf8(documents.At(index).path);
		}

		std::vector<std::string> CurrentIdentities(const Core::DocumentSet& documents)
		{
			std::vector<std::string> identities;
			identities.reserve(documents.GetCount());

			for (std::size_t index = 0; index < documents.GetCount(); ++index)
				identities.push_back(Identity(documents, index));

			return identities;
		}
	}

	void DocumentWindows::RequestOpen(const std::filesystem::path& path, bool permanent)
	{
		const Core::OpenOutcome outcome = m_Documents.Open(path, permanent);

		// Already open: the set has activated it, but nothing about that reaches ImGui,
		// which goes on showing whichever tab it last focused - so ask Render to bring
		// this one forward. See m_FocusRequest.
		if (outcome == Core::OpenOutcome::Activated)
			m_FocusRequest = path;

		if (outcome != Core::OpenOutcome::NeedsConfirmation)
			return;

		AskAboutSize(path, permanent);
	}

	void DocumentWindows::LoadConfirmed(const std::filesystem::path& path, bool permanent)
	{
		const auto existing = m_Documents.Find(path);

		// A tab can already be there with nothing in it: one opened on a file that did
		// not exist, whose file has since appeared and appeared large. OpenConfirmed
		// would find that tab and only activate it, loading nothing and leaving the
		// question answered by an empty tab - so the item is filled directly instead.
		if (existing && !m_Documents.At(*existing).document)
		{
			// Confirmed, so NeedsConfirmation cannot come back, and Loaded and Failed both
			// leave the tab saying the right thing: the content, or the reason there is
			// none. Nothing here to branch on.
			static_cast<void>(Core::LoadFromFile(m_Documents.At(*existing), true));
			return;
		}

		m_Documents.OpenConfirmed(path, permanent);
	}

	void DocumentWindows::AskAboutSize(const std::filesystem::path& path, bool permanent)
	{
		LargeFileQuestion question{path, permanent, 0};

		// Probed once, here, rather than every frame the modal is open: a stat plus an
		// 8 KB read per frame is real work when the file sits on a network share, and the
		// answer cannot change between the question and the reply in any way worth
		// redrawing for. Whoever asks has just probed the same file successfully - that is
		// what NeedsConfirmation means - so this second probe is asking a question already
		// answered, and a failure here means the file went away in between.
		if (const auto probe = Core::ProbeFile(path))
		{
			question.size = probe.GetValue().size;
		}

		// Guarded the way the reload and close questions are: two events naming the same
		// file the same way - a double dispatch from the tree, an event replayed after a
		// workspace reload, a watcher batch and a focus regain both finding the same file
		// waiting to be loaded - are one question, not the same one asked twice in a row.
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

		// RewritePaths only ever changes paths in place, never the count or the order, so
		// the two snapshots line up index for index.
		for (std::size_t index = 0; index < newIdentities.size(); ++index)
		{
			const std::string& oldIdentity = oldIdentities[index];
			const std::string& newIdentity = newIdentities[index];

			if (newIdentity == oldIdentity)
				continue;

			unsigned int target = 0;

			// The live window, if this document was ever drawn under its old identity, is
			// authoritative for where it currently sits.
			if (ImGuiWindow* oldWindow = ImGui::FindWindowByName(oldIdentity.c_str());
				oldWindow != nullptr && oldWindow->DockNode != nullptr)
			{
				target = oldWindow->DockNode->ID;
			}
			else if (const auto pending = m_PendingDockTargets.find(oldIdentity);
					 pending != m_PendingDockTargets.end())
			{
				// The root moved again before a Render ever consumed the previous move's
				// target - chain it forward instead of leaving a dead entry keyed on an
				// identity nothing will ever look up again.
				target = pending->second;
			}

			m_PendingDockTargets.erase(oldIdentity);

			if (target != 0)
				m_PendingDockTargets[newIdentity] = target;
		}
	}

	void DocumentWindows::HandleFilesChanged(
		const std::vector<std::filesystem::path>& directories, bool everything)
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			// The watcher names directories, so an item is worth a stat only when its own
			// directory is one of them. `everything` means the batch could not be narrowed
			// down at all - dropped notifications, or a git state change - and every open
			// document has to be checked. Items already marked Missing are stated too
			// (see Core::SyncWithFile): the file may have come back.
			if (!everything && std::find(directories.begin(), directories.end(), item.path.parent_path()) == directories.end())
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
			// Ask rather than reload, and let the question queue, so several files
			// changing in the same batch (a git checkout) are asked about one at a time
			// instead of every modal fighting to open this frame. Two sweeps naming the
			// same file - a watcher batch and a focus regain moments later - are one
			// question, not the same one asked twice over.
			//
			// Copied here rather than above the switch: every open document is reconciled
			// on a focus regain and on every "everything" watcher batch, and all but a
			// handful of those end as Unchanged, which has no use for a path.
			const std::filesystem::path path = m_Documents.At(index).path;

			if (!m_ReloadConflict.Asked(path))
			{
				m_ReloadConflict.Ask(path);
			}

			break;
		}

		case Core::FileSyncOutcome::SafeToReload:
			// The same function the conflict modal's "Reload from disk" button calls, so
			// a reload that happened without asking and one the user asked for say the
			// same thing afterwards - including what stopped it, when something did.
			ReloadOne(index);
			break;

		case Core::FileSyncOutcome::NeedsLoad:
		{
			// The file a tab was opened on before it existed is here now. Loading it is
			// what turns the tab from a cleared missing mark into the file's content;
			// without it the tab would show nothing and no later sync would look again.
			Core::OpenItem& item = m_Documents.At(index);

			if (Core::LoadFromFile(item, false) == Core::ItemLoad::NeedsConfirmation)
			{
				// It came back large. The same question a large file gets when it is
				// opened by hand, and answering it reaches the same loader.
				AskAboutSize(item.path, !item.preview);
			}

			break;
		}

		case Core::FileSyncOutcome::Unchanged:
		case Core::FileSyncOutcome::WentMissing:
			// Nothing to say. A file that went away is already said by the tab's own
			// title, which gains and loses "(deleted)" with the mark.
			break;
		}
	}

	void DocumentWindows::ReloadOne(std::size_t index)
	{
		const std::string fileName = Core::ToUtf8(m_Documents.At(index).path.filename());

		if (const auto result = Core::ReloadItem(m_Documents, index))
		{
			SetTransientMessage(fileName + " reloaded (changed on disk)");
		}
		else
		{
			SetTransientMessage("Could not reload " + fileName + ": " + result.GetError().message);
		}
	}

	bool DocumentWindows::SaveOne(std::size_t index)
	{
		const Core::OpenItem& item = m_Documents.At(index);

		// Checked here rather than left to SaveDocument, which succeeds having written
		// nothing: an item carrying a reason instead of a document has nothing to save,
		// and "Widget.obj saved" would be a message about a write that never happened.
		if (!item.document)
		{
			return true;
		}

		const std::string fileName = Core::ToUtf8(item.path.filename());

		if (const auto result = Core::SaveDocument(m_Documents, index))
		{
			SetTransientMessage(fileName + " saved");
			return true;
		}
		else
		{
			SetTransientMessage("Could not save " + fileName + ": " + result.GetError().message);
			return false;
		}
	}

	void DocumentWindows::RequestSave(std::size_t index)
	{
		Core::OpenItem& item = m_Documents.At(index);

		if (!item.document)
			return;

		if (item.document->CheckChangedOnDisk())
		{
			// Never noticed: a dropped notification, with no chance to have asked
			// earlier. This is the last safety net, immediately before the write.
			if (!m_OverwriteConfirm.Asked(item.path))
			{
				m_OverwriteConfirm.Ask(item.path);
			}

			return;
		}

		SaveOne(index);
	}

	bool DocumentWindows::SaveAllModified()
	{
		// Qualified: this member has the same name, and an unqualified call from inside it
		// would find itself. The names match on purpose - this is that operation plus the
		// sentence about it - and the qualification is what keeps them apart.
		const Core::SaveAllReport report = Core::SaveAllModified(m_Documents);

		// A stopped batch says why, naming the document that stopped it, and gets no
		// summary over the top: a count of what was saved before the failure would only
		// bury the one thing the user has to act on. RequestSave (a deliberate
		// per-document Ctrl+S) is still where a conflicted document gets its
		// Overwrite/Cancel question, which is why nothing is asked here.
		if (report.stoppedAt)
		{
			SetTransientMessage("Could not save " + Core::ToUtf8(report.stoppedAt->path.filename()) + ": " + report.stoppedAt->error.message);

			// Every exit here answers "did everything reach disk" with report.IsComplete()
			// rather than with a literal. The branches below differ in what they say, not
			// in what they conclude, and a second failure mode added to SaveAllReport must
			// not be able to reach IsComplete() while this wrapper keeps returning true.
			return report.IsComplete();
		}

		if (report.conflicted.empty())
		{
			if (report.saved > 0)
			{
				SetTransientMessage("Saved " + std::to_string(report.saved) + (report.saved == 1 ? " file." : " files."));
			}

			return report.IsComplete();
		}

		std::vector<std::string> names;
		names.reserve(report.conflicted.size());

		for (const std::filesystem::path& path : report.conflicted)
		{
			names.push_back(Core::ToUtf8(path.filename()));
		}

		// Named together in one message, not one message per file overwriting the last:
		// "Saved 4 files. 1 changed on disk and was not saved: Widget.cpp" tells the user
		// exactly what still needs attention instead of leaving them to assume "saved"
		// meant all of them.
		std::string message;

		if (report.saved > 0)
		{
			message = "Saved " + std::to_string(report.saved) + (report.saved == 1 ? " file. " : " files. ");
		}

		message += std::to_string(names.size()) + " changed on disk and " + (names.size() == 1 ? "was" : "were") + " not saved: " + Join(names);

		SetTransientMessage(message);

		return report.IsComplete();
	}

	bool DocumentWindows::HasModifiedDocuments() const
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.document && item.document->IsModified())
			{
				return true;
			}
		}

		return false;
	}

	void DocumentWindows::RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys)
	{
		commands.Add({.id = "document.saveAll",
					  .label = "Save All",
					  .run = [this] { (void)SaveAllModified(); },
					  .enabled = [this] { return HasModifiedDocuments(); }});

		keys.SetDefault("document.saveAll",
						{.primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});
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

	void DocumentWindows::Render(const CommandContext& context, float fontSize,
								 Core::IndentStyle indentStyle,
								 unsigned int fallbackDockNode, bool wordWrap)
	{
		// Identities as they stand at the start of this frame - after RequestOpen,
		// CloseAll or HandleRootMoved, all of which run earlier in the same
		// OnUpdate/OnImGuiRender pass and never inside this function.
		const std::vector<std::string> startingIdentities = CurrentIdentities(m_Documents);

		// Taken and cleared in one step: whatever asked for this focus did so earlier in
		// this same pass, so a request that matches no document this frame never will, and
		// leaving it set would hand a stolen focus to whichever document next claims that
		// path.
		const std::optional<std::filesystem::path> focusRequest =
			std::exchange(m_FocusRequest, std::nullopt);

		// Every question below asks the gate before opening and keeps whatever it was
		// asked about pending when refused - see ModalGate for why they cannot simply all
		// open. Neither that nor the check for a popup ImGui no longer has open is written
		// out here: ModalQuestion::Begin does both for every question there is, so no
		// question drawn from this function can leave the mechanism behind it wedged.
		if (m_SizeQuestion.Begin())
		{
			const LargeFileQuestion* question = m_SizeQuestion.GetCurrent();
			const std::uint64_t size = question != nullptr ? question->size : 0;

			// A size of zero means the probe in RequestOpen failed, which after a
			// successful one moments earlier means the file has gone. Claiming "this file
			// is 0 MB" would be asking permission for a size nobody knows.
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

		// The reload-conflict, overwrite-confirm and close-confirm modals below all live
		// here, before the per-document loop, for the same reason as the size question
		// above: OpenPopup/BeginPopupModal must not run from inside a Begin/BeginChild
		// pair that may not be current when the popup needs to draw, and this is the one
		// place in Render() that is never nested inside either. The loop below only ever
		// asks the questions drawn here; the actual ImGui calls happen here.

		if (m_ReloadConflict.Begin())
		{
			const std::filesystem::path* path = m_ReloadConflict.GetCurrent();
			const std::string fileName =
				path != nullptr ? Core::ToUtf8(path->filename()) : std::string{};

			ImGui::Text("%s changed on disk, and this tab has unsaved changes.",
						fileName.c_str());
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

			// The entire acknowledgement mechanism: recording the file's current state as
			// known is what stops Ctrl+S, or the next sweep, asking about this file again.
			if (ImGui::Button("Keep my version"))
			{
				if (path != nullptr)
				{
					if (const auto index = m_Documents.Find(*path);
						index && m_Documents.At(*index).document)
					{
						m_Documents.At(*index).document->AcceptDiskState();
					}
				}

				m_ReloadConflict.Answer();
			}

			m_ReloadConflict.End();
		}

		if (m_OverwriteConfirm.Begin())
		{
			const std::filesystem::path* path = m_OverwriteConfirm.GetCurrent();
			const std::string fileName =
				path != nullptr ? Core::ToUtf8(path->filename()) : std::string{};

			ImGui::Text("%s changed on disk since it was opened. Overwrite it?",
						fileName.c_str());
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

		// Asked only of a document still there to ask about, and still dirty: one closed
		// some other way (WorkspaceClosedEvent, say) while its question waited its turn
		// has nothing left to save, and neither has one saved in the meantime. Checked
		// when the question comes off the queue rather than when it joined, since all of
		// that can happen while it waits. A file merely deleted from disk in the meantime
		// still gets its question: MarkMissing leaves the item and its in-memory content
		// in place, so Save (recreating the file) and Discard both still mean something.
		//
		// A tab that is still open and no longer dirty is closed here rather than merely
		// dropped. The question exists because the user pressed a close button, and that
		// request outlives the reason to ask about it: a document saved with Ctrl+S while
		// its question waited has nothing left to ask, but the tab was still asked to
		// close, and leaving it open would answer the user's click with silence.
		const auto stillModified = [this](const std::filesystem::path& candidate) {
			const auto index = m_Documents.Find(candidate);

			if (!index)
			{
				return false;
			}

			const Core::OpenItem& item = m_Documents.At(*index);

			if (item.document && item.document->IsModified())
			{
				return true;
			}

			ImGui::ClearWindowSettings(WindowName(m_Documents, *index).c_str());
			m_Documents.Close(*index);

			return false;
		};

		if (m_CloseConfirm.Begin(stillModified))
		{
			const std::filesystem::path* path = m_CloseConfirm.GetCurrent();
			const std::string fileName =
				path != nullptr ? Core::ToUtf8(path->filename()) : std::string{};

			ImGui::Text("Save changes to %s before closing?", fileName.c_str());
			ImGui::Separator();

			// Shared by Save and Discard: finds the tab again by path (it may have moved
			// since the question was asked, though never far in practice) and closes it
			// the same way the loop below would have. Called before Answer(), which drops
			// the payload `path` points into.
			const auto finishClose = [this, path] {
				if (path == nullptr)
				{
					return;
				}

				if (const auto index = m_Documents.Find(*path))
				{
					ImGui::ClearWindowSettings(WindowName(m_Documents, *index).c_str());
					m_Documents.Close(*index);
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

				if (saved)
				{
					finishClose();
					m_CloseConfirm.Answer();
				}

				// A failed save changes nothing: the document stays open and dirty, the
				// reason is already in the status bar via SaveOne, and the question stays
				// up so the user can try again or choose Discard/Cancel instead.
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

		for (std::size_t index = 0; index < m_Documents.GetCount();)
		{
			Core::OpenItem& item = m_Documents.At(index);
			const std::string identity = Identity(m_Documents, index);
			const std::string name = WindowName(m_Documents, index);

			// Whether ImGui has ever heard of this window is the wrong question:
			// ClearWindowSettings (below, and in the purge at the end of this function)
			// undocks a window but does not destroy it, so a document closed and reopened
			// reuses the same ImGuiWindow object FindWindowByName would find - stripped of
			// its dock, with nothing left to redock it. Whether this identity is new
			// *since last frame* is the right question: false for a document that has
			// been open continuously, which is what stops a user's drag from being
			// fought every frame, and true both the first time a document is ever seen
			// and every time it reappears after being closed.
			const bool justOpened = std::find(m_PreviousIdentities.begin(),
											  m_PreviousIdentities.end(), identity) == m_PreviousIdentities.end();

			if (justOpened)
			{
				// Checked, not merely non-zero: splitting the editor area, opening a file
				// in the new split and closing it leaves ImGui free to delete the emptied
				// node, and the id kept here outlives it. Docking to a dead id does not
				// fail - DockBuilderDockWindow creates a fresh node under no parent - so
				// the window floats free instead of landing in the centre.
				const bool lastFocusedAlive =
					m_LastFocusedNode != 0 && ImGui::DockBuilderGetNode(m_LastFocusedNode) != nullptr;

				if (!lastFocusedAlive)
					m_LastFocusedNode = 0;

				unsigned int targetNode = lastFocusedAlive ? m_LastFocusedNode : fallbackDockNode;

				// A window carrying over a dock position from before its path was
				// rewritten takes priority over both: it is more specific than "wherever
				// was last focused", and right even when nothing has been focused yet.
				if (const auto pending = m_PendingDockTargets.find(identity);
					pending != m_PendingDockTargets.end())
				{
					targetNode = pending->second;
					m_PendingDockTargets.erase(pending);
				}

				if (targetNode != 0)
					ImGui::DockBuilderDockWindow(name.c_str(), targetNode);
			}

			const bool preview = item.preview;

			// A preview is marked by colour alone. Italic was tried and removed:
			// it could only ever reach this window's own title bar, drawn inside Begin()
			// below when the window is floating, because a docked window's tab is drawn
			// earlier in the frame from inside DockSpace(), and ImGui draws every tab of a
			// node in one pass with one global font - so no font can mark one docked tab
			// and not its neighbours. It would also have collided with the icon now in
			// every title: the MDI glyphs are merged onto the regular mono face alone, so a
			// title drawn in any other face renders its icon as a missing glyph.
			//
			// Colour has neither problem: Begin() -> BeginDocked() calls
			// StoreDockStyleForWindow, which snapshots the style colours in effect at that
			// moment into window->DockStyle, and the tab bar swaps the global style to each
			// window's own snapshot before drawing its tab. So colours pushed here, before
			// Begin, land on this document's tab and no other.
			//
			// The label carries the marker, and the icon in it dims along with the name.
			// That is a cost, not an oversight: a tab's text is one string drawn by one
			// RenderTextEllipsis in one colour, and the icon is a glyph inside that same
			// string, so no per-glyph colour is available without rewriting ImGui's tab
			// rendering.
			//
			// Fading the tab's background instead was tried, and it keeps the icon bright
			// but says the wrong thing: a preview is nearly always the active tab, and an
			// active tab is by construction the brightest item on the bar - lighter fill
			// plus an overline. Fading those only softens a highlight that still reads as
			// "this one is on top" next to a darker inactive neighbour, and fading it far
			// enough to lose that would hide which file is actually open.
			//
			// Position was tried here too, via ImGuiWindowClass::TabItemFlagsOverrideSet,
			// and removed: ImGuiTabItemFlags_Trailing only orders a tab last, it does not
			// pin it to the bar's right edge. TabBarLayout takes ImMin of the right-aligned
			// offset and the packed one, so the trailing section sits straight after the
			// central one whenever the bar has room to spare, and the flag changed nothing
			// visible.
			if (preview)
			{
				ImGui::PushStyleColor(ImGuiCol_Text,
									  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}

			// The dot next to the tab/title, and - for a docked tab - what stops ImGui
			// assuming the X means "close now": the window keeps being torn down from the
			// dock's bookkeeping only if Render stops resubmitting it, which is exactly
			// what the dirty-close branch below refuses to do until the question resolves.
			const bool modified = item.document && item.document->IsModified();
			const ImGuiWindowFlags windowFlags =
				modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;

			// Picked in the tree while already open. Begin() is what turns this into a
			// selected tab on a docked window, which is why it is set here rather than
			// ImGui::SetWindowFocus being called from RequestOpen against a window that
			// may not have been submitted yet this frame.
			if (focusRequest == item.path)
				ImGui::SetNextWindowFocus();

			bool open = true;
			const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

			// Popped the moment Begin returns, so the dimming reaches this document's tab
			// and title bar without following it into the editor's own contents.
			if (preview)
				ImGui::PopStyleColor();

			if (visible)
			{
				if (ImGui::IsWindowFocused())
				{
					m_Documents.Activate(index);

					if (ImGuiWindow* window = ImGui::GetCurrentWindow();
						window != nullptr && window->DockNode != nullptr)
						m_LastFocusedNode = window->DockNode->ID;
				}

				// A double click on the tab promotes a preview, the same as one in the
				// tree does.
				if (preview && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
					m_Documents.Promote(index);

				const EditorFrameResult editorResult =
					DrawEditor(context, item, fontSize, indentStyle, wordWrap);

				// The first edit promotes a preview tab exactly as a double click does:
				// once the user has typed into it, it is no longer something they were
				// just glancing at.
				if (editorResult.edited && preview)
					m_Documents.Promote(index);

				if (editorResult.saveRequested)
					RequestSave(index);
			}

			ImGui::End();

			if (!open)
			{
				if (modified)
				{
					// Both versions are somebody's work: not closed here. The tab keeps
					// being submitted every frame (falling through to ++index below)
					// until its turn comes up in the queue above and Save, Discard or
					// Cancel resolves it. Queued rather than replacing whatever is
					// already pending, so a second tab's close button pressed before the
					// first question resolves still gets its own question, in order,
					// instead of being silently dropped - and the tab's close button
					// pressed again while its own question waits does not queue it twice.
					if (!m_CloseConfirm.Asked(item.path))
					{
						m_CloseConfirm.Ask(item.path);
					}
				}
				else
				{
					// The layout entry goes with the window: position belongs to the
					// group, not to the file, so a reopened document lands where the user
					// is working rather than where it was months ago - and imgui.ini
					// stops collecting a dead section per file ever opened.
					ImGui::ClearWindowSettings(name.c_str());
					m_Documents.Close(index);
					continue;
				}
			}

			++index;
		}

		// Purges whatever left the document set since the last time this ran without
		// going through this loop's own close button above - see m_PreviousIdentities.
		// Deliberately runs after the loop: a renamed document's pending dock target has
		// to be consumed by Begin() - inside the loop above - before its old identity's
		// dock node can safely be undocked and possibly deleted here. Purging first would
		// let ClearWindowSettings tear down a shared node before DockBuilderDockWindow's
		// deferred settings entry for the new identity ever gets to bind to it, stranding
		// the renamed document in a fresh orphan node instead of its old spot.
		for (const std::string& previousIdentity : m_PreviousIdentities)
		{
			if (std::find(startingIdentities.begin(), startingIdentities.end(), previousIdentity) == startingIdentities.end())
				ImGui::ClearWindowSettings(previousIdentity.c_str());
		}

		m_PreviousIdentities = CurrentIdentities(m_Documents);
	}
}
#include "Editor/DocumentWindows.h"

#include "Core/Base/Base.h"

#include "Editor/ActiveTextDocument.h"
#include "Editor/Navigation/DefinitionNavigation.h"
#include "Editor/EditorCommands.h"
#include "Editor/TabColour.h"
#include "Theme/Palettes.h"
#include "Editor/TextEditorWidget.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/LspClient.h"

#include "Persistence/CaretRestore.h"

#include "UI/ClipboardCopy.h"
#include "UI/TabWindow.h"
#include "UI/ToastEvent.h"

#include "Core/App/Redraw.h"
#include "Core/Base/ByteSize.h"
#include "Core/Base/Log.h"
#include "Core/Base/Result.h"
#include "Core/Base/Strings.h"
#include "Core/Command/Chord.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/Platform/Clipboard.h"
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
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {
	namespace {
		constexpr double kTransientMessageSeconds = 4.0;
		constexpr unsigned int kNoDockNode = 0;
		constexpr float kNoRunOffset = 0.0f;
		constexpr float kNoRunSpacing = 0.0f;

		// WARNING: ImGui shows what precedes ### and identifies by what follows, so the path stays
		// out of sight and the icon moves no window id. The MDI glyphs are merged onto the regular
		// mono face alone, so nothing drawing this title may push another font.
		std::string WindowName(const Core::DocumentSet& documents, std::size_t index)
		{
			const std::filesystem::path& path = documents.At(index).Path;
			const std::string icon{Core::IconForExtension(Core::ToUtf8(path.extension()))};
			const std::string title = documents.DisplayTitle(index);

			return icon + "  " + title + "###" + Core::ToUtf8(path);
		}

		std::string DocumentIdentityOf(const Core::DocumentSet& documents, std::size_t index)
		{
			return Core::ToUtf8(documents.At(index).Path);
		}

		std::vector<std::string> CurrentIdentities(const Core::DocumentSet& documents)
		{
			std::vector<std::string> identities;
			identities.reserve(documents.GetCount());

			for (std::size_t index = 0; index < documents.GetCount(); ++index)
			{
				identities.push_back(DocumentIdentityOf(documents, index));
			}

			return identities;
		}

		bool LiesInAnyOf(const std::vector<std::filesystem::path>& directories,
						 const std::filesystem::path& path)
		{
			return std::find(directories.begin(), directories.end(), path.parent_path()) != directories.end();
		}

		bool ContainsSamePath(const std::vector<std::filesystem::path>& paths,
							  const std::filesystem::path& wanted)
		{
			const std::filesystem::path normalizedWanted = Core::NormalizePath(wanted);

			for (const std::filesystem::path& path : paths)
			{
				if (Core::SamePath(Core::NormalizePath(path), normalizedWanted))
				{
					return true;
				}
			}

			return false;
		}

		void DrawColouredRun(const std::string& text, std::size_t from, std::size_t to, ImU32 colour,
							 bool& isFirstRun)
		{
			if (to <= from || from >= text.size())
			{
				return;
			}

			if (!isFirstRun)
			{
				ImGui::SameLine(kNoRunOffset, kNoRunSpacing);
			}

			isFirstRun = false;

			const std::size_t end = std::min(to, text.size());
			const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(colour);

			ImGui::TextColored(rgba, "%.*s", static_cast<int>(end - from), text.data() + from);
		}

		std::string FileNameOf(const std::filesystem::path& path)
		{
			return Core::ToUtf8(path.filename());
		}

		std::string SavedCount(std::size_t saved)
		{
			if (saved == 0)
			{
				return {};
			}

			return "Saved " + std::to_string(saved) + (saved == 1 ? " file." : " files.");
		}

		std::string NotSavedSummary(const std::vector<std::filesystem::path>& conflicted)
		{
			std::vector<std::string> names;
			names.reserve(conflicted.size());

			for (const std::filesystem::path& path : conflicted)
			{
				names.push_back(Core::ToUtf8(path.filename()));
			}

			return std::to_string(names.size()) + " changed on disk and " + (names.size() == 1 ? "was" : "were") + " not saved: " + Core::JoinedWith(names, ", ");
		}
	}

	std::string SaveFailureMessage(const std::filesystem::path& path, std::string_view error)
	{
		if (const std::string name = Core::ToUtf8(path.filename()); !name.empty() && error.find(name) != std::string_view::npos)
		{
			return std::string(error);
		}

		return Core::ToUtf8(path) + ": " + std::string(error);
	}

	void DocumentWindows::RequestOpen(const std::filesystem::path& path, bool permanent)
	{
		const Core::TabKind tabKind = permanent ? Core::TabKind::Permanent : Core::TabKind::Preview;

		const Core::OpenOutcome outcome = m_Documents.Open(path, tabKind);

		if (DocumentState* const state = FindState(path))
		{
			state->CarriedOver = false;
		}

		if (outcome != Core::OpenOutcome::NeedsConfirmation)
		{
			return;
		}

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

			const DocumentState* const kept = FindState(item.Path);

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

		const std::optional<std::size_t> activeIndex = m_Documents.GetActiveIndex();

		if (activeIndex && m_Documents.At(*activeIndex).Document &&
			!IsCarriedOver(m_Documents.At(*activeIndex).Path))
		{
			state.Active = m_Documents.At(*activeIndex).Path;
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
			if (DocumentState* const kept = FindState(tab.Path))
			{
				kept->CarriedOver = false;
			}

			std::error_code fileCheckError;

			if (!std::filesystem::is_regular_file(tab.Path, fileCheckError))
			{
				APP_INFO("Not reopening '{}': it is no longer there", Core::ToUtf8(tab.Path));
				continue;
			}

			if (m_Documents.Find(tab.Path))
			{
				continue;
			}

			LoadConfirmed(tab.Path, !tab.Preview);

			const std::optional<std::size_t> itemIndex = m_Documents.Find(tab.Path);

			if (!itemIndex)
			{
				continue;
			}

			Core::OpenItem& item = m_Documents.At(*itemIndex);

			item.PendingFocus = false;
			m_RestoredIdentities.insert(DocumentIdentityOf(m_Documents, *itemIndex));

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
			const std::optional<std::size_t> activeIndex = m_Documents.Find(*state.Active);

			if (activeIndex)
			{
				m_Documents.Activate(*activeIndex);
			}
		}
	}

	void DocumentWindows::LoadConfirmed(const std::filesystem::path& path, bool permanent)
	{
		const std::optional<std::size_t> existing = m_Documents.Find(path);

		if (existing && !m_Documents.At(*existing).Document)
		{
			static_cast<void>(Core::LoadFromFile(m_Documents.At(*existing), Core::LargeFileConfirmation::Confirmed));
			return;
		}

		const Core::TabKind tabKind = permanent ? Core::TabKind::Permanent : Core::TabKind::Preview;
		m_Documents.OpenConfirmed(path, tabKind);
	}

	void DocumentWindows::AskAboutSize(const std::filesystem::path& path, bool permanent)
	{
		const std::uint64_t unknownSize = 0;
		LargeFileQuestion question{.Path = path, .Permanent = permanent, .Size = unknownSize};

		if (const Core::Result<Core::FileProbe> probe = Core::ProbeFile(path))
		{
			question.Size = probe.GetValue().Size;
		}

		m_SizeQuestion.AskOnce(std::move(question));
	}

	void DocumentWindows::HandleRootMoved(const std::filesystem::path& from,
										  const std::filesystem::path& to)
	{
		const std::vector<std::string> oldIdentities = CurrentIdentities(m_Documents);

		m_Documents.RewritePaths(from, to);

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
			const std::optional<std::filesystem::path> relative = Core::RelativeTo(path, from);
			const std::filesystem::path movedPath = *relative == "." ? to : to / *relative;

			StateFor(movedPath).CarriedOver = true;
		}

		const std::vector<std::string> newIdentities = CurrentIdentities(m_Documents);

		for (std::size_t index = 0; index < newIdentities.size(); ++index)
		{
			if (newIdentities[index] != oldIdentities[index])
			{
				MovePendingDockTarget(oldIdentities[index], newIdentities[index]);
			}
		}
	}

	unsigned int DocumentWindows::DockNodeForIdentity(const std::string& identity) const
	{
		if (ImGuiWindow* window = ImGui::FindWindowByName(identity.c_str());
			window != nullptr && window->DockNode != nullptr)
		{
			return window->DockNode->ID;
		}

		if (m_PendingDockTargets.contains(identity))
		{
			return m_PendingDockTargets.at(identity);
		}

		return kNoDockNode;
	}

	void DocumentWindows::MovePendingDockTarget(const std::string& oldIdentity,
												const std::string& newIdentity)
	{
		const unsigned int target = DockNodeForIdentity(oldIdentity);

		m_PendingDockTargets.erase(oldIdentity);

		if (target != kNoDockNode)
		{
			m_PendingDockTargets[newIdentity] = target;
		}
	}

	void DocumentWindows::HandleFilesChanged(
		const std::vector<std::filesystem::path>& directories, bool everything)
	{
		ReloadBatch batch;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
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
		if (batch.AnyFailed)
		{
			return;
		}

		const std::vector<std::size_t>& reloaded = batch.Expected;

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
		if (ReloadOne(index, ReloadAnnouncement::Silent))
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

			if (m_ExpectedWrites.TakeFor(path))
			{
				ReloadExpected(index, batch);
				break;
			}

			m_ReloadConflict.AskOnce(path);

			break;
		}

		case Core::FileSyncOutcome::SafeToReload:
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
			Core::OpenItem& item = m_Documents.At(index);

			if (Core::LoadFromFile(item, Core::LargeFileConfirmation::Unconfirmed) == Core::ItemLoad::NeedsConfirmation)
			{
				AskAboutSize(item.Path, !item.Preview);
			}

			break;
		}

		case Core::FileSyncOutcome::Unchanged:
		case Core::FileSyncOutcome::WentMissing:
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

	bool DocumentWindows::ReloadOne(std::size_t index, ReloadAnnouncement announcement)
	{
		const std::string fileName = Core::ToUtf8(m_Documents.At(index).Path.filename());

		if (const Core::Result<void> result = Core::ReloadItem(m_Documents, index))
		{
			AnnounceReload(m_Documents.At(index));

			if (announcement == ReloadAnnouncement::Announce)
			{
				SetTransientMessage(fileName + " reloaded (changed on disk)");
			}

			return true;
		}
		else
		{
			APP_ERROR("Could not reload {}: {}", fileName, result.GetError().Message);

			ReportFailure(Core::ToastKind::Error, "Reload failed", result.GetError().Message);
			return false;
		}
	}

	bool DocumentWindows::SaveOne(std::size_t index)
	{
		const Core::OpenItem& item = m_Documents.At(index);

		if (!item.Document)
		{
			return true;
		}

		const std::string fileName = Core::ToUtf8(item.Path.filename());

		if (const Core::Result<void> result = Core::SaveDocument(m_Documents, index))
		{
			m_Server.SaveDocument(item.Path, LanguageOfPath(item.Path));

			SetTransientMessage(fileName + " saved");
			return true;
		}
		else
		{
			ReportSaveFailure(item.Path, result.GetError());
			return false;
		}
	}

	void DocumentWindows::ReportSaveFailure(const std::filesystem::path& path, const Core::Error& error)
	{
		APP_ERROR("Could not save '{}': {}", Core::ToUtf8(path), error.Message);

		ReportFailure(Core::ToastKind::Error, "Save failed", SaveFailureMessage(path, error.Message));
	}

	void DocumentWindows::ReportFailure(Core::ToastKind kind, std::string title, std::string_view message)
	{
		m_Events.Publish(FolderToast(kind, std::move(title), message, m_WorkspaceRoot));
	}

	void DocumentWindows::RequestSave(std::size_t index)
	{
		Core::OpenItem& item = m_Documents.At(index);

		if (!item.Document)
		{
			return;
		}

		if (item.Document->CheckChangedOnDisk())
		{
			m_OverwriteConfirm.AskOnce(item.Path);

			return;
		}

		SaveOne(index);
	}

	void DocumentWindows::DrawFileTabItems(std::size_t index, const std::filesystem::path& workspaceRoot)
	{
		const Core::OpenItem& item = m_Documents.At(index);

		const bool unsaved = item.Document && item.Document->IsModified();

		const bool selected = false;

		if (ImGui::MenuItem("Save", nullptr, selected, unsaved))
		{
			RequestSave(index);
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Path"))
		{
			CopyToClipboard(m_Events, "path", Core::ToUtf8(item.Path));
		}

		if (ImGui::MenuItem("Copy Relative Path"))
		{
			std::error_code code;
			const std::filesystem::path relative =
				workspaceRoot.empty() ? std::filesystem::path{}
									  : std::filesystem::relative(item.Path, workspaceRoot, code);

			const bool usable = !workspaceRoot.empty() && !code && !relative.empty();
			CopyToClipboard(m_Events, "relative path", Core::ToUtf8(usable ? relative : item.Path));
		}

		if (ImGui::MenuItem("Copy File Name"))
		{
			CopyToClipboard(m_Events, "file name", Core::ToUtf8(item.Path.filename()));
		}

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const Core::Result<void> revealed = Core::Shell::RevealInFileBrowser(item.Path); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
				ReportFailure(Core::ToastKind::Error, "Cannot reveal file", revealed.GetError().Message);
			}
		}
	}

	bool DocumentWindows::SaveAllModified()
	{
		const Core::SaveAllReport report = Core::SaveAllModified(m_Documents);

		if (report.StoppedAt)
		{
			ReportSaveFailure(report.StoppedAt->Path, report.StoppedAt->Error);

			return report.IsComplete();
		}

		if (report.Saved > 0)
		{
			SetTransientMessage(SavedCount(report.Saved));
		}

		if (!report.Conflicted.empty())
		{
			ReportFailure(Core::ToastKind::Warning, "Save skipped", NotSavedSummary(report.Conflicted));
		}

		return report.IsComplete();
	}

	bool DocumentWindows::SaveDocuments(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			const std::optional<std::size_t> index = FindOpen(path);

			if (index && !SaveOne(*index))
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
			const std::optional<std::size_t> index = FindOpen(path);

			if (!index)
			{
				continue;
			}

			if (ReloadOne(*index, ReloadAnnouncement::Silent))
			{
				++reverted;
			}
			else
			{
				failed = true;
			}
		}

		m_ReloadConflict.WithdrawIf([&paths](const std::filesystem::path& queued) { return ContainsSamePath(paths, queued); });

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
					  .Run = [this] { SaveAllModified(); },
					  .Enabled = [this] { return HasModifiedDocuments(); }});

		keys.SetDefault("document.saveAll",
						{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)});

		const bool fromChord = false;
		const bool fromMenu = true;

		for (const EditorCommandSpec& spec : EditorCommandSpecs())
		{
			const EditorAction action = spec.Action;

			commands.Add({.Id = spec.Id,
						  .Label = spec.Label,
						  .Run = [this, action, fromChord] { QueueOnActiveDocument(action, fromChord); },
						  .RunFromMenu = [this, action, fromMenu] { QueueOnActiveDocument(action, fromMenu); },
						  .Enabled = [this, action] { return CanRunOnActiveDocument(action); },
						  .KeyScope = Core::ShortcutScope::FocusedEditor,
						  .RepeatsWhileHeld = spec.Repeats});

			keys.SetDefault(spec.Id, BindingOf(spec));
		}

		commands.Add({.Id = kSaveCommand,
					  .Label = "Save",
					  .Run = [this] { RequestSaveOfActiveDocument(); },
					  .Enabled = [this] { return ActiveTabHoldsDocument(); },
					  .KeyScope = Core::ShortcutScope::FocusedEditor,
					  .RepeatsWhileHeld = false});

		keys.SetDefault(kSaveCommand, SaveBinding());

		commands.Add({.Id = kCloseCommand,
					  .Label = "Close Tab",
					  .Run = [this] { RequestCloseOfActiveTab(); },
					  .Enabled = [this] { return FindActiveTextItem() != nullptr; },
					  .KeyScope = Core::ShortcutScope::FocusedEditor,
					  .RepeatsWhileHeld = false});

		keys.SetDefault(kCloseCommand, CloseBinding());

		commands.Add({.Id = kSwitchHeaderSourceCommand,
					  .Label = "Switch Header/Source",
					  .Run = [this] { RequestSwitchSourceHeaderOfActiveDocument(); },
					  .Enabled = [this] { return CanSwitchSourceHeaderOfActiveDocument(); },
					  .RepeatsWhileHeld = false});

		keys.SetDefault(kSwitchHeaderSourceCommand,
						{.Primary = Core::Chord::Key(ImGuiMod_Alt | ImGuiKey_O)});
	}

	std::optional<std::size_t> DocumentWindows::FindActiveTextIndex()
	{
		const std::optional<std::filesystem::path> path = ActiveTextDocument(m_Documents);

		return path ? m_Documents.Find(*path) : std::nullopt;
	}

	Core::OpenItem* DocumentWindows::FindActiveTextItem()
	{
		const std::optional<std::size_t> index = FindActiveTextIndex();

		return index ? &m_Documents.At(*index) : nullptr;
	}

	void DocumentWindows::QueueOnActiveDocument(EditorAction action, bool fromMenu)
	{
		if (const std::optional<std::size_t> index = FindActiveTextIndex())
		{
			QueueEditorAction(*index, action, fromMenu);
		}
	}

	// WARNING: Only Paste asks the clipboard, and it asks without reading it: ImGui's read opens
	// the clipboard and copies it, and logs an error whenever it holds something other than text.
	bool DocumentWindows::CanRunOnActiveDocument(EditorAction action)
	{
		const Core::OpenItem* item = FindActiveTextItem();
		const bool hasText = action == EditorAction::Paste && Core::ClipboardHasText();

		return item != nullptr && CanRun(action, *item, hasText);
	}

	void DocumentWindows::RequestSaveOfActiveDocument()
	{
		if (const std::optional<std::size_t> index = FindActiveTextIndex())
		{
			RequestSave(*index);
		}
	}

	bool DocumentWindows::ActiveTabHoldsDocument()
	{
		const Core::OpenItem* item = FindActiveTextItem();

		return item != nullptr && item->Document.has_value();
	}

	void DocumentWindows::RequestCloseOfActiveTab()
	{
		if (const Core::OpenItem* item = FindActiveTextItem())
		{
			m_CloseRequested = item->Path;
			Core::Redraw::Request();
		}
	}

	void DocumentWindows::RequestSwitchSourceHeaderOfActiveDocument()
	{
		if (const std::optional<std::size_t> index = FindActiveTextIndex())
		{
			RequestSwitchSourceHeader(*index);
		}
	}

	bool DocumentWindows::CanSwitchSourceHeaderOfActiveDocument()
	{
		const Core::OpenItem* item = FindActiveTextItem();

		if (item == nullptr)
		{
			return false;
		}

		const LanguageId language = LanguageOfPath(item->Path);

		return m_Server.HasChosenServer(language) && m_Server.OffersSwitchSourceHeader(language);
	}

	void DocumentWindows::QueueEditorAction(std::size_t index, EditorAction action, bool fromMenu)
	{
		if (index >= m_Documents.GetCount())
		{
			return;
		}

		StateFor(m_Documents.At(index).Path).Widget.PendingActions.push_back({action, fromMenu});

		Core::Redraw::Request();
	}

	void DocumentWindows::RequestSwitchSourceHeader(std::size_t index)
	{
		if (index >= m_Documents.GetCount())
		{
			return;
		}

		m_Router.AskSwitchSourceHeader(m_Documents.At(index).Path);
	}

	void DocumentWindows::SetTransientMessage(std::string text)
	{
		m_TransientMessage =
			TransientMessage{std::move(text), ImGui::GetTime() + kTransientMessageSeconds};
	}

	std::optional<std::string> DocumentWindows::GetStatusMessage() const
	{
		if (!m_TransientMessage)
		{
			return std::nullopt;
		}

		if (ImGui::GetTime() >= m_TransientMessage->ExpiresAt)
		{
			return std::nullopt;
		}

		return m_TransientMessage->Text;
	}

	void DocumentWindows::CloseAndForgetLayout(std::size_t index)
	{
		ImGui::ClearWindowSettings(WindowName(m_Documents, index).c_str());
		m_RestoredIdentities.erase(DocumentIdentityOf(m_Documents, index));
		m_Documents.Close(index);
		PruneDocumentStates();
	}

	void DocumentWindows::CloseIfOpen(const std::filesystem::path& path)
	{
		if (const std::optional<std::size_t> index = m_Documents.Find(path))
		{
			CloseAndForgetLayout(*index);
		}
	}

	void DocumentWindows::RenderSizeQuestion()
	{
		const LargeFileQuestion* asked = m_SizeQuestion.Begin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const LargeFileQuestion question = *asked;
		const std::uint64_t size = question.Size;

		if (size == 0)
		{
			ImGui::TextUnformatted("This file can no longer be read. Load it anyway?");
		}
		else
		{
			ImGui::Text("This file is %s. Load it anyway?", Core::FormatWholeMegabytes(size).c_str());
		}
		ImGui::Separator();

		if (ImGui::Button("Load"))
		{
			LoadConfirmed(question.Path, question.Permanent);
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
		const std::filesystem::path* asked = m_ReloadConflict.Begin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const std::filesystem::path path = *asked;

		ImGui::Text("%s changed on disk, and this tab has unsaved changes.", FileNameOf(path).c_str());
		ImGui::Separator();

		if (ImGui::Button("Reload from disk (lose my changes)"))
		{
			if (const std::optional<std::size_t> index = m_Documents.Find(path))
			{
				ReloadOne(*index);
			}

			m_ReloadConflict.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Keep my version"))
		{
			if (const std::optional<std::size_t> index = m_Documents.Find(path); index && m_Documents.At(*index).Document)
			{
				m_Documents.At(*index).Document->AcceptDiskState();
			}

			m_ReloadConflict.Answer();
		}

		m_ReloadConflict.End();
	}

	void DocumentWindows::RenderOverwriteConfirm()
	{
		const std::filesystem::path* asked = m_OverwriteConfirm.Begin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const std::filesystem::path path = *asked;

		ImGui::Text("%s changed on disk since it was opened. Overwrite it?", FileNameOf(path).c_str());
		ImGui::Separator();

		if (ImGui::Button("Overwrite"))
		{
			if (const std::optional<std::size_t> index = m_Documents.Find(path))
			{
				SaveOne(*index);
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
		const std::optional<std::size_t> index = m_Documents.Find(path);

		if (!index)
		{
			return false;
		}

		const Core::OpenItem& item = m_Documents.At(*index);

		if (item.Document && item.Document->IsModified())
		{
			return true;
		}

		CloseAndForgetLayout(*index);

		return false;
	}

	void DocumentWindows::RenderCloseConfirm()
	{
		const std::filesystem::path* asked = m_CloseConfirm.Begin(
			[this](const std::filesystem::path& candidate) { return StillNeedsCloseConfirmation(candidate); });

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const std::filesystem::path path = *asked;

		ImGui::Text("Save changes to %s before closing?", FileNameOf(path).c_str());
		ImGui::Separator();

		if (ImGui::Button("Save"))
		{
			bool saved = true;

			if (const std::optional<std::size_t> index = m_Documents.Find(path))
			{
				saved = SaveOne(*index);
			}

			if (saved)
			{
				CloseIfOpen(path);
				m_CloseConfirm.Answer();
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Discard"))
		{
			CloseIfOpen(path);
			m_CloseConfirm.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_CloseConfirm.Answer();
		}

		m_CloseConfirm.End();
	}

	// WARNING: ImGui::ClearWindowSettings undocks a window but does not destroy it, so a document
	// closed and reopened reuses its ImGuiWindow. Asking ImGui whether it has heard of the window
	// would call that reopened document known; only a document absent last frame is new.
	bool DocumentWindows::IsNewSinceLastFrame(const std::string& identity) const
	{
		return std::find(m_PreviousIdentities.begin(), m_PreviousIdentities.end(), identity) == m_PreviousIdentities.end();
	}

	unsigned int DocumentWindows::ChooseDockTarget(const std::string& identity,
												   unsigned int fallbackDockNode)
	{
		// WARNING: The node must be checked alive, not just non-zero. ImGui may delete a node
		// emptied by closing a split, and DockBuilderDockWindow does not fail on a dead id: it
		// creates a parentless node and the window floats free.
		const bool lastFocusedAlive =
			m_LastFocusedNode != kNoDockNode && ImGui::DockBuilderGetNode(m_LastFocusedNode) != nullptr;

		if (!lastFocusedAlive)
		{
			m_LastFocusedNode = kNoDockNode;
		}

		if (m_PendingDockTargets.contains(identity))
		{
			const unsigned int target = m_PendingDockTargets.at(identity);
			m_PendingDockTargets.erase(identity);

			return target;
		}

		return lastFocusedAlive ? m_LastFocusedNode : fallbackDockNode;
	}

	bool DocumentWindows::IsShown(const std::filesystem::path& path) const
	{
		return m_Shown.contains(path);
	}

	void DocumentWindows::ActivateAndRememberNode(std::size_t index)
	{
		m_Documents.Activate(index);

		if (ImGuiWindow* window = ImGui::GetCurrentWindow();
			window != nullptr && window->DockNode != nullptr)
		{
			m_LastFocusedNode = window->DockNode->ID;
		}
	}

	DocumentWindows::DocumentState& DocumentWindows::StateFor(const std::filesystem::path& path)
	{
		return m_States[path];
	}

	DocumentWindows::DocumentState* DocumentWindows::FindState(const std::filesystem::path& path)
	{
		return m_States.contains(path) ? &m_States.at(path) : nullptr;
	}

	const DocumentWindows::DocumentState* DocumentWindows::FindState(const std::filesystem::path& path) const
	{
		return m_States.contains(path) ? &m_States.at(path) : nullptr;
	}

	DocumentWindows::LensState* DocumentWindows::FindLens(const std::filesystem::path& path)
	{
		DocumentState* const state = FindState(path);

		return state != nullptr && state->Lens ? &*state->Lens : nullptr;
	}

	bool DocumentWindows::IsCarriedOver(const std::filesystem::path& path) const
	{
		const DocumentState* const state = FindState(path);

		return state != nullptr && state->CarriedOver;
	}

	SyntaxCache& DocumentWindows::SyntaxFor(const Core::OpenItem& item)
	{
		std::optional<SyntaxCache>& syntax = StateFor(item.Path).Syntax;

		if (!syntax)
		{
			syntax.emplace();
			syntax->SetSyntax(SyntaxOf(LanguageOfPath(item.Path)));
			syntax->SetTabWidth(item.TabWidth);
		}

		return *syntax;
	}

	DocumentWindows::LensState* DocumentWindows::LensFor(const Core::OpenItem& item)
	{
		const LanguageId language = LanguageOfPath(item.Path);

		if (!LanguageHasServers(language) || !item.Document)
		{
			return nullptr;
		}

		DocumentState& state = StateFor(item.Path);

		if (!state.Lens)
		{
			state.Lens.emplace();

			m_Server.EnsureStarted(item.Path, language);
			m_Server.OpenDocument(item.Path, language, item.Document->GetBuffer().Materialize());

			m_Router.Track(item.Path);

			RestoreAnalysis(item, state);
		}

		return &*state.Lens;
	}

	void DocumentWindows::PruneDocumentStates()
	{
		std::vector<std::filesystem::path> closedPaths;

		for (const auto& [path, state] : m_States)
		{
			if (!m_Documents.Find(path))
			{
				closedPaths.push_back(path);
			}
		}

		for (const std::filesystem::path& path : closedPaths)
		{
			const DocumentState& closedState = m_States.at(path);

			if (const std::optional<LensState>& lens = closedState.Lens)
			{
				m_Server.CloseDocument(path, LanguageOfPath(path));

				KeepAnalysis(path, *lens, closedState.Semantic);
				m_Router.Forget(path);
			}

			m_States.erase(path);
		}

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

		// WARNING: A lens anchors to a row and this runs before the document has drawn once, so
		// its row map is empty and every line maps to row zero. Anchoring now would collapse the
		// restored declarations onto one row; the rebuild a frame later anchors them properly.
		lens.NeedsRebuild = true;

		state.Semantic = std::move(restored->Semantic);
	}

	std::optional<std::filesystem::file_time_type> DocumentWindows::ModifiedTimeOf(
		const std::filesystem::path& path)
	{
		std::error_code timeError;
		const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, timeError);

		// WARNING: An unreadable stamp must be empty, never a default stamp: two default stamps
		// compare equal and would restore an entry nothing ever checked.
		if (timeError)
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

		if (!item.Document)
		{
			return overlay;
		}

		const std::map<std::filesystem::path, DiagnosticsModel::Entry>& diagnosticEntries =
			m_Diagnostics.GetDocuments();

		const std::uint64_t noGeneration = 0;
		const std::uint64_t generation = diagnosticEntries.contains(item.Path)
											 ? diagnosticEntries.at(item.Path).Generation
											 : noGeneration;
		const std::size_t revision = item.Document->GetBuffer().GetRevision();

		DiagnosticGuard& guard = state.DiagnosticsBuiltFrom;

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

		m_Server.ChangeDocument(item.Path, language, item.Document->GetBuffer().Materialize());

		LensState* const lens = FindLens(item.Path);

		if (lens == nullptr)
		{
			return;
		}

		LensState& state = *lens;

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
		// WARNING: A count is asked once and kept, so it must not be asked while the server still learns the workspace. Roslyn loading its projects answers an empty list, and clangd mid-index counts only the files it has reached; neither is an error to retry.
		if (!m_Router.CanCountReferences(item.Path))
		{
			return;
		}

		const std::size_t margin = frame.LastVisibleRow - frame.FirstVisibleRow + 1;
		const std::size_t firstRowWanted =
			frame.FirstVisibleRow > margin ? frame.FirstVisibleRow - margin : 0;
		const std::size_t endRowWanted = frame.LastVisibleRow + margin + 1;

		for (const std::size_t row : state.Model.RowsNeedingCounts(firstRowWanted, endRowWanted))
		{
			if (state.Asked.contains(row))
			{
				continue;
			}

			state.Asked.insert(row);

			const Lens* lens = state.Model.LensAt(row);

			if (lens == nullptr)
			{
				continue;
			}

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

		if (m_Router.NoteServerState(item.Path))
		{
			state.Asked.clear();
		}

		if (frame.Edited)
		{
			m_Server.ChangeDocument(item.Path, LanguageOfPath(item.Path),
									item.Document->GetBuffer().Materialize());

			m_Router.WantSymbolsNow(item.Path);

			DropInFlightCounts(item.Path, state);
		}

		if (state.HaveDeclarations && (state.NeedsRebuild || state.BuiltAt != item.ColumnsPerWrappedRow))
		{
			state.Model.SetDeclarations(state.Declarations, item.Rows,
										item.Document->GetBuffer(), item.TabWidth);
			state.BuiltAt = item.ColumnsPerWrappedRow;
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

		const std::optional<std::size_t> index = m_Documents.Find(path);

		if (!index || !m_Documents.At(*index).Document)
		{
			return false;
		}

		const Core::OpenItem& item = m_Documents.At(*index);
		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		state.Declarations = answer.Declarations;

		state.HaveDeclarations = true;
		state.Model.SetDeclarations(state.Declarations, item.Rows, buffer, item.TabWidth);
		state.BuiltAt = item.ColumnsPerWrappedRow;

		state.Asked.clear();

		return true;
	}

	void DocumentWindows::ApplySemanticAnswer(const std::filesystem::path& path,
											  const LanguageServices::Answer& answer)
	{
		const std::optional<std::size_t> index = m_Documents.Find(path);

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

		if (!m_Server.HasChosenServer(language))
		{
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

		const DocumentState* const state = FindState(item.Path);
		const CaretScreenPosition screenPosition =
			state != nullptr ? state->CaretScreen : CaretScreenPosition{};

		m_Router.AskDefinition(DefinitionQuestion{.Path = item.Path,
												  .CaretOffset = item.Caret.Offset,
												  .From = where,
												  .Screen = screenPosition});
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
		if (!m_Server.HasChosenServer(language))
		{
			SetTransientMessage("no references found");
			return;
		}

		m_Router.AskReferences(item.Path, where, symbol);

		panel.ShowWorking(symbol);
		panel.Focus();
	}

	void DocumentWindows::ApplyHoverAnswer(const HoverQuestion& question,
										   const LanguageServices::Answer& answer)
	{
		DocumentState* const state = FindState(question.Path);

		if (state == nullptr)
		{
			return;
		}

		if (answer.Failed || answer.Text.empty())
		{
			APP_TRACE("Hover at '{}' {}:{} {}", Core::ToUtf8(question.Path), question.Line + 1,
					  question.TokenFrom + 1, answer.Failed ? "failed" : "answered nothing");
		}

		HoldHoverAnswer(state->Hover, question, answer);

		Core::Redraw::Request();
	}

	void DocumentWindows::ApplyPreviewAnswer(const HoverQuestion& question,
											 const LanguageServices::Answer& answer)
	{
		DocumentState* const state = FindState(question.Path);

		if (state == nullptr)
		{
			return;
		}

		if (answer.Failed)
		{
			APP_TRACE("Definition preview at '{}' {}:{} failed", Core::ToUtf8(question.Path),
					  question.Line + 1, question.TokenFrom + 1);
		}

		HoldPreviewAnswer(state->Hover, question, answer, m_Documents);

		Core::Redraw::Request();
	}

	void DocumentWindows::ApplySwitchSourceHeaderAnswer(const std::filesystem::path& from,
														const LanguageServices::Answer& answer)
	{
		if (answer.Failed || answer.Locations.empty())
		{
			APP_INFO("No header/source counterpart for {}", Core::ToUtf8(from));
			return;
		}

		const bool permanent = true;
		RequestOpen(answer.Locations.front().Path, permanent);
	}

	void DocumentWindows::ApplyDefinitionAnswer(const DefinitionQuestion& question,
												const LanguageServices::Answer& answer)
	{
		const std::optional<std::size_t> index = m_Documents.Find(question.Path);

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
				Jump(m_Documents, candidates.front(), [this](const std::filesystem::path& path) { RequestOpen(path, true); });
			}
			break;

		case DefinitionOutcome::NeedsChoice:
			// WARNING: Opening this popup while a modal question is up would tear that question
			// down, because ImGui cannot stack two popups at level 0. Dropping the answer is the
			// cheaper loss: F12 asks again with one keystroke.
			if (!m_Modals.TryOpen(kDefinitionChoicesTitle))
			{
				SetTransientMessage("close the open dialog, then press F12 again");
				break;
			}

			m_DefinitionChoices = std::move(candidates);
			m_DefinitionChoiceLines.clear();

			m_DefinitionChoicesAt = question.Screen;

			ImGui::OpenPopup(kDefinitionChoicesTitle);
			break;
		}
	}

	// WARNING: The position is kept in m_DefinitionChoicesAt and spent here, immediately before the
	// Begin that consumes it: every BeginPopupModal drawn earlier in the frame clears ImGui's
	// next-window state on its early-out. ImGui also closes a popup on Escape or an outside click
	// without telling whoever opened it, so the gate is released here when the popup is gone.
	void DocumentWindows::RenderDefinitionChoices(const ResolvedScheme& scheme,
												  std::size_t tabWidth)
	{
		ImGui::SetNextWindowPos(ImVec2(m_DefinitionChoicesAt.X, m_DefinitionChoicesAt.Y),
								ImGuiCond_Appearing);

		if (!ImGui::BeginPopup(kDefinitionChoicesTitle))
		{
			if (m_Modals.HoldsFor(kDefinitionChoicesTitle) &&
				!ImGui::IsPopupOpen(kDefinitionChoicesTitle))
			{
				m_DefinitionChoices.clear();
				m_DefinitionChoiceLines.clear();
				m_Modals.Close(kDefinitionChoicesTitle);
			}

			return;
		}

		const EditorPalette editorPalette = ReadEditorPalette(scheme);
		const SyntaxPalette syntaxPalette = ReadSyntaxPalette(scheme);

		const bool linesCurrent = m_DefinitionChoiceLines.size() == m_DefinitionChoices.size() && m_DefinitionChoiceLinesTabWidth == tabWidth;

		if (!linesCurrent)
		{
			m_DefinitionChoiceLines.clear();

			for (const Location& choice : m_DefinitionChoices)
			{
				m_DefinitionChoiceLines.push_back(LoadCandidateLine(m_Documents, choice, tabWidth));
			}

			m_DefinitionChoiceLinesTabWidth = tabWidth;
		}

		for (std::size_t index = 0; index < m_DefinitionChoices.size(); ++index)
		{
			const Location& location = m_DefinitionChoices[index];

			ImGui::PushID(static_cast<int>(index));

			const std::string header = Core::ToUtf8(location.Path.filename()) + ":" +
									   std::to_string(location.Line + 1);

			if (ImGui::Selectable(header.c_str()))
			{
				Jump(m_Documents, location, [this](const std::filesystem::path& path) { RequestOpen(path, true); });
				ImGui::CloseCurrentPopup();
			}

			const CandidateLine& choiceLine = m_DefinitionChoiceLines[index];

			if (!choiceLine.Text.empty())
			{
				const std::string& expanded = choiceLine.Text;
				const std::vector<Token>& tokens = choiceLine.Tokens;

				ImGui::Indent();

				const std::size_t lineStart = 0;
				std::size_t drawnUpTo = lineStart;
				bool isFirstRun = true;

				if (tokens.empty())
				{
					DrawColouredRun(expanded, lineStart, expanded.size(), editorPalette.Text, isFirstRun);
				}
				else
				{
					for (const Token& token : tokens)
					{
						DrawColouredRun(expanded, drawnUpTo, token.From, editorPalette.Text, isFirstRun);
						DrawColouredRun(expanded, token.From, token.To, syntaxPalette.Of(token.Kind), isFirstRun);
						drawnUpTo = token.To;
					}

					DrawColouredRun(expanded, drawnUpTo, expanded.size(), editorPalette.Text, isFirstRun);
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

			void ApplyHover(const HoverQuestion& question,
							const LanguageServices::Answer& answer) override
			{
				m_Windows.ApplyHoverAnswer(question, answer);
			}

			void ApplyPreviewDefinition(const HoverQuestion& question,
										const LanguageServices::Answer& answer) override
			{
				m_Windows.ApplyPreviewAnswer(question, answer);
			}

			void ApplySwitchSourceHeader(const std::filesystem::path& from,
										 const LanguageServices::Answer& answer) override
			{
				m_Windows.ApplySwitchSourceHeaderAnswer(from, answer);
			}

			void ApplyReferences(const std::string& symbol,
								 const LanguageServices::Answer& answer) override
			{
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
			{
				ImGui::ClearWindowSettings(previousIdentity.c_str());
			}
		}
	}

	void DocumentWindows::RenderDocumentWindows(
		const CommandContext& context, float fontSize, Core::IndentStyle indentStyle,
		unsigned int fallbackDockNode, bool wordWrap, ChangeMarkerDisplay changeDisplay,
		HoverSettings hoverSettings, const ResolvedScheme& scheme, ReferencesPanel& referencesPanel,
		Core::TabCloseRequests& closeRequests, const std::filesystem::path& workspaceRoot)
	{
		m_Shown.clear();

		for (std::size_t index = 0; index < m_Documents.GetCount();)
		{
			Core::OpenItem& item = m_Documents.At(index);
			const std::string identity = DocumentIdentityOf(m_Documents, index);
			const std::string name = WindowName(m_Documents, index);

			const bool appearing = IsNewSinceLastFrame(identity);
			const bool noPendingFocus = false;

			TabWindowState window{.Fresh = appearing,
								  .Restored = appearing && m_RestoredIdentities.erase(identity) > 0,
								  .Focus = std::exchange(item.PendingFocus, noPendingFocus)};

			const PlaceNewTab place = [this, &identity, fallbackDockNode](const std::string& windowName) {
				DockInto(ChooseDockTarget(identity, fallbackDockNode))(windowName);
			};

			const bool preview = item.Preview;

			// WARNING: The colour is pushed before Begin: BeginDocked snapshots the style colours in
			// effect, and the tab bar draws each tab from its window's own snapshot. A font cannot
			// do the same, since every tab of a node is drawn in one pass with one global font.
			const GitDecoration decoration =
				item.Path.empty() ? GitDecoration::None : m_GitDecorations.FileOf(item.Path);
			const std::optional<ImU32> tabColour =
				TabTextColour(decoration, preview, m_GitDecorations.ColourFor(decoration),
							  ImGui::GetColorU32(ImGuiCol_TextDisabled));
			if (tabColour)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, *tabColour);
			}

			const bool modified = item.Document && item.Document->IsModified();
			const ImGuiWindowFlags windowFlags =
				modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;

			const TabWindowFrame frame = BeginTabWindow(name, window, place, closeRequests, windowFlags);
			const bool visible = frame.Visible;
			bool open = frame.Open;

			if (tabColour)
			{
				ImGui::PopStyleColor();
			}

			if (m_CloseRequested && *m_CloseRequested == item.Path)
			{
				m_CloseRequested.reset();
				open = false;
			}

			Core::DrawTabContextMenu(closeRequests,
									 [this, index, &workspaceRoot] { DrawFileTabItems(index, workspaceRoot); });

			if (visible)
			{
				m_Shown.insert(item.Path);

				// WARNING: Child windows count, because a click on the text focuses the editor
				// surface, a child of this window. Without the flag the right half of a split stayed
				// active and commands acted on the half nobody was looking at.
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
				{
					ActivateAndRememberNode(index);
				}

				const ImGuiMouseButton leftMouseButton = ImGuiMouseButton_Left;

				if (preview && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(leftMouseButton))
				{
					m_Documents.Promote(index);
				}

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

				view.Semantic = &SemanticFor(item);

				if (DiagnosticOverlay& diagnostics = DiagnosticFor(item); !diagnostics.IsEmpty())
				{
					view.Diagnostics = &diagnostics;
				}

				view.DiagnosticsStale = m_Diagnostics.IsStale(item.Path);

				const bool wantsChanges = changeDisplay.Gutter || changeDisplay.Scrollbar;

				if (wantsChanges && item.Document)
				{
					m_ChangeMarkers.Update(item.Path, item.Document->GetBuffer());
				}

				const Core::Ref<const ChangeMarkers> changes =
					wantsChanges ? m_ChangeMarkers.Get(item.Path) : nullptr;
				view.Changes = changes.get();

				view.Anchor = &state.Anchor;
				view.Widget = &state.Widget;

				view.Hover = lens != nullptr ? &state.Hover : nullptr;
				view.HoverConfig = hoverSettings;
				view.HoverSemantics = &m_PreviewSemantics;

				if (state.Hover.Preview)
				{
					const std::optional<std::size_t> shownIndex = m_Documents.Find(state.Hover.Preview->Path);

					if (shownIndex)
					{
						view.HoverPreviewOverlay = &SemanticFor(m_Documents.At(*shownIndex));
					}
				}

				std::optional<PendingTopLine>& pendingTop = state.PendingTop;

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

				if (pendingTop && editorResult.DrewRows && pendingTop->Settle(*view.Anchor))
				{
					pendingTop.reset();
				}

				state.CaretScreen =
					CaretScreenPosition{.X = editorResult.CaretScreenX, .Y = editorResult.CaretScreenY};

				if (lens != nullptr)
				{
					UpdateLenses(item, *lens, editorResult);
				}

				if (editorResult.Edited && preview)
				{
					m_Documents.Promote(index);
				}

				if (editorResult.DefinitionRequested)
				{
					RequestDefinition(index);
				}

				if (lens != nullptr && editorResult.LensClicked)
				{
					RequestReferencesForLens(item, *lens, *editorResult.LensClicked, referencesPanel);
				}

				if (editorResult.CopiedFromMenu)
				{
					ReportCopied(m_Events, "selection", *editorResult.CopiedFromMenu);
				}

				if (editorResult.CancelHover)
				{
					m_Router.CancelHover(item.Path);
				}

				if (editorResult.AskHover)
				{
					m_Router.AskHover(*editorResult.AskHover);
				}

				if (editorResult.AskPreview)
				{
					m_Router.AskPreviewDefinition(*editorResult.AskPreview);
				}
			}

			// WARNING: Checked again after the editor draws: a chord bound to the close command runs
			// from inside its editor's draw, after the check above had already looked.
			if (m_CloseRequested && *m_CloseRequested == item.Path)
			{
				m_CloseRequested.reset();
				open = false;
			}

			ImGui::End();

			if (!open)
			{
				if (modified)
				{
					m_CloseConfirm.AskOnce(item.Path);
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
		m_WorkspaceRoot = frame.WorkspaceRoot;

		// WARNING: The order is load-bearing. Answers come first so they are on screen this frame,
		// and popups open before any Begin. A renamed document's pending dock target must be
		// consumed by the Begin in RenderDocumentWindows before its old identity's node is
		// undocked, so ForgetLayoutOfWindowsClosedElsewhere runs after the loop.
		const std::vector<std::string> startingIdentities = CurrentIdentities(m_Documents);

		CollectServerAnswers(referencesPanel);

		RenderSizeQuestion();
		RenderReloadConflict();
		RenderOverwriteConfirm();
		RenderCloseConfirm();
		RenderDefinitionChoices(frame.Scheme, frame.Indent.Width);

		RenderDocumentWindows(context, frame.FontSize, frame.Indent, frame.FallbackDockNode,
							  frame.WordWrap, frame.ChangeMarkers, frame.Hover, frame.Scheme,
							  referencesPanel,
							  frame.CloseRequests, frame.WorkspaceRoot);

		m_CloseRequested.reset();

		PruneDocumentStates();

		ForgetLayoutOfWindowsClosedElsewhere(startingIdentities);

		m_PreviousIdentities = CurrentIdentities(m_Documents);
	}
}

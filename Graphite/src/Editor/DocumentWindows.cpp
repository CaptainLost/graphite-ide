#include "Editor/DocumentWindows.h"

#include "Core/Base/Base.h"

#include "Editor/ActiveTextDocument.h"
#include "Editor/Breadcrumb.h"
#include "Editor/EditorSettings.h"
#include "Editor/Navigation/DefinitionNavigation.h"
#include "Editor/Navigation/SelectInDocument.h"
#include "Editor/Overlays/LensWaiting.h"
#include "Editor/EditorCommands.h"
#include "Editor/TabColour.h"
#include "Theme/Palettes.h"
#include "Editor/TextEditorWidget.h"

#include "Image/ImageTabView.h"
#include "Image/ImageTabs.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/LspClient.h"
#include "Language/Server/LspProtocol.h"
#include "Language/Server/ServerWaitingText.h"

#include "Persistence/CaretRestore.h"

#include "UI/ClipboardCopy.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/FileIcons.h"
#include "UI/FileTabLook.h"
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
#include "Core/Events/FrameInput.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContentWindowColours.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
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
#include <chrono>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {
	namespace {
		constexpr double kTransientMessageSeconds = 4.0;
		constexpr unsigned int kNoDockNode = 0;
		constexpr bool kNotRepeating = false;
		constexpr float kNoRunOffset = 0.0f;
		constexpr float kNoRunSpacing = 0.0f;

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

		// A tab shown as text or as a picture comes back after a restart; one whose file could not be read does not.
		bool IsKeptAcrossRestart(const Core::OpenItem& item)
		{
			return item.Document != nullptr || Core::IsLoading(item) || item.View == Core::ItemView::Image;
		}

		// An image item whose file has gone draws the failure message, as a text tab does.
		bool ShowsPicture(const Core::OpenItem& item)
		{
			return item.View == Core::ItemView::Image && item.Failure == Core::OpenFailure::None;
		}

		std::string SavedCount(std::size_t saved)
		{
			if (saved == 0)
			{
				return {};
			}

			return "Saved " + Core::Counted(saved, "file") + ".";
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

		void PlaceRestoredCaret(Core::OpenItem& item, std::size_t line, std::size_t column, Core::ColumnUnit columnIn)
		{
			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const std::size_t byteColumn =
				columnIn == Core::ColumnUnit::Utf16CodeUnit ? ByteColumnOfUtf16Column(buffer, line, Utf16Column(column)) : column;
			const std::size_t offset = RestoredCaretOffset(buffer, line, byteColumn);

			item.Caret = Core::Caret{.Offset = offset, .Anchor = offset};
		}

		TabState TabStillBeingRead(const Core::OpenItem& item, std::size_t topLine)
		{
			TabState tab{.Path = item.Path, .Preview = item.Preview, .Top = topLine};

			if (item.CaretOnLoad)
			{
				tab.Line = item.CaretOnLoad->Line;
				tab.Column = item.CaretOnLoad->Column;
				tab.ColumnIn = item.CaretOnLoad->ColumnIn;
			}

			return tab;
		}

		Core::WrapColumnsForLineCount WrapColumnsOfTallDocumentIn(const WrapGeometry& geometry)
		{
			return [geometry](std::size_t lineCount) { return WrappedColumns(geometry, lineCount, VerticalScrollbar::Shown); };
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

	// WARNING: ImGui shows what precedes ### and identifies by what follows, so the path stays
	// out of sight and the icon moves no window id.
	std::string DocumentWindowName(const Core::DocumentSet& documents, std::size_t index)
	{
		const std::filesystem::path& path = documents.At(index).Path;
		const std::string icon{GlyphOf(KindOfFile(path))};
		const std::string title = documents.DisplayTitle(index);

		return icon + "  " + title + "###" + Core::ToUtf8(path);
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

			if (!IsKeptAcrossRestart(item))
			{
				continue;
			}

			const DocumentState* const kept = FindState(item.Path);

			if (kept != nullptr && kept->CarriedOver)
			{
				continue;
			}

			if (Core::IsLoading(item))
			{
				const std::size_t topOfFile = 0;
				const std::size_t topLine = kept != nullptr ? kept->TopLineOnLoad.value_or(topOfFile) : topOfFile;
				state.Tabs.push_back(TabStillBeingRead(item, topLine));
				continue;
			}

			TabState tab;
			tab.Path = item.Path;
			tab.Preview = item.Preview;

			// A picture has no caret and no scroll to keep.
			if (item.Document)
			{
				const Core::Position caret = item.Document->GetBuffer().ToPosition(item.Caret.Offset);
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
			}

			state.Tabs.push_back(std::move(tab));
		}

		const std::optional<std::size_t> activeIndex = m_Documents.GetActiveIndex();

		if (activeIndex && IsKeptAcrossRestart(m_Documents.At(*activeIndex)) &&
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

			if (Core::IsLoading(item))
			{
				item.CaretOnLoad = Core::CaretWhenLoaded{
					.Line = tab.Line,
					.Column = tab.Column,
					.ColumnIn = tab.ColumnIn,
					.Place = [line = tab.Line, column = tab.Column, columnIn = tab.ColumnIn](Core::OpenItem& loaded) {
						PlaceRestoredCaret(loaded, line, column, columnIn);
					}};
				StateFor(tab.Path).TopLineOnLoad = tab.Top;
				continue;
			}

			if (!item.Document)
			{
				continue;
			}

			PlaceRestoredCaret(item, tab.Line, tab.Column, tab.ColumnIn);
			KeepRestoredTopLine(item, tab.Top);
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

	void DocumentWindows::KeepRestoredTopLine(const Core::OpenItem& item, std::size_t savedTopLine)
	{
		if (const std::size_t top = RestoredTopLine(item.Document->GetBuffer(), savedTopLine); top > 0)
		{
			StateFor(item.Path).PendingTop = PendingTopLine{.Line = top, .CaretOffset = item.Caret.Offset};
		}
	}

	void DocumentWindows::LoadConfirmed(const std::filesystem::path& path, bool permanent)
	{
		const std::optional<std::size_t> existing = m_Documents.Find(path);

		if (existing && !m_Documents.At(*existing).Document)
		{
			m_Documents.LoadAgain(*existing, Core::LargeFileConfirmation::Confirmed);
			return;
		}

		const Core::TabKind tabKind = permanent ? Core::TabKind::Permanent : Core::TabKind::Preview;
		m_Documents.OpenConfirmed(path, tabKind);
	}

	// WARNING: A file changed while it was read is caught here, not by the watcher event that came during the read.
	void DocumentWindows::LandFinishedLoads()
	{
		ReloadBatch batch;

		for (const std::size_t landedIndex : m_Documents.TakeFinishedLoads())
		{
			FinishStateOfRead(landedIndex);
			RespondToSync(landedIndex, Core::SyncWithFile(m_Documents, landedIndex), batch);
		}

		AnnounceExpectedReloads(batch);
	}

	void DocumentWindows::FinishStateOfRead(std::size_t index)
	{
		const Core::OpenItem& item = m_Documents.At(index);
		DocumentState* const state = FindState(item.Path);

		if (state == nullptr)
		{
			return;
		}

		state->PaneWrapWhileRead.reset();
		state->WrapHandedToRead.reset();

		if (!state->TopLineOnLoad)
		{
			return;
		}

		const std::size_t savedTopLine = *state->TopLineOnLoad;
		state->TopLineOnLoad.reset();

		KeepRestoredTopLine(item, savedTopLine);
	}

	// WARNING: A read whose own pane has drawn wraps for that pane; the last drawn editor stands in until it has.
	void DocumentWindows::HandWrapToLoads(bool wordWrap)
	{
		const std::optional<WrapGeometry> wantedByDefault = wordWrap ? m_WrapOfLastDrawnEditor : std::nullopt;

		if (wantedByDefault != m_WrapHandedToLoads)
		{
			m_Documents.SetWrapForLoads(wantedByDefault ? WrapColumnsOfTallDocumentIn(*wantedByDefault) : Core::WrapColumnsForLineCount{});
			m_WrapHandedToLoads = wantedByDefault;
		}

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			HandWrapOfItsPaneToRead(index, wordWrap);
		}
	}

	void DocumentWindows::HandWrapOfItsPaneToRead(std::size_t index, bool wordWrap)
	{
		const Core::OpenItem& item = m_Documents.At(index);

		if (!item.LoadTicket)
		{
			return;
		}

		DocumentState* const state = FindState(item.Path);

		if (state == nullptr || !state->PaneWrapWhileRead)
		{
			return;
		}

		const ReadWrap wanted{.Ticket = *item.LoadTicket, .Geometry = wordWrap ? state->PaneWrapWhileRead : std::nullopt};

		if (state->WrapHandedToRead == wanted)
		{
			return;
		}

		m_Documents.SetWrapForLoad(index, wanted.Geometry ? WrapColumnsOfTallDocumentIn(*wanted.Geometry) : Core::WrapColumnsForLineCount{});
		state->WrapHandedToRead = wanted;
	}

	void DocumentWindows::RetryLoad(std::size_t index)
	{
		if (m_Documents.RetryLoad(index) == Core::ItemLoad::NeedsConfirmation)
		{
			const Core::OpenItem& item = m_Documents.At(index);
			AskAboutSize(item.Path, !item.Preview);
		}
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
			CheckPictureOnDisk(index);
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
			CheckPictureOnDisk(index);
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
			const Core::OpenItem& item = m_Documents.At(index);

			if (m_Documents.LoadAgain(index, Core::LargeFileConfirmation::Unconfirmed) == Core::ItemLoad::NeedsConfirmation)
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
			m_DocumentStates.NoteSaved(item.Path);

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
			m_OverwriteConfirm.AskOnce(OverwriteQuestion{.Path = item.Path});

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

		for (const std::filesystem::path& saved : report.Saved)
		{
			m_DocumentStates.NoteSaved(saved);
		}

		if (report.StoppedAt)
		{
			ReportSaveFailure(report.StoppedAt->Path, report.StoppedAt->Error);

			return report.IsComplete();
		}

		if (!report.Saved.empty())
		{
			SetTransientMessage(SavedCount(report.Saved.size()));
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
			const bool tabHoldsText = index && m_Documents.At(*index).Document != nullptr;

			if (!tabHoldsText)
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
			SetTransientMessage("Discarded unsaved changes in " + Core::Counted(reverted, "file"));
		}
	}

	bool DocumentWindows::HasModifiedDocuments() const
	{
		for (const std::shared_ptr<Core::TextDocument>& document : m_Documents.GetStore().GetDocuments())
		{
			if (document->IsModified())
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

		RegisterEditingCommands(commands, keys, m_ActiveEditor);

		commands.Add({.Id = kSwitchHeaderSourceCommand,
					  .Label = "Switch Header/Source",
					  .Run = [this] { RequestSwitchSourceHeaderOfActiveDocument(); },
					  .Enabled = [this] { return CanSwitchSourceHeaderOfActiveDocument(); },
					  .RepeatsWhileHeld = false});

		keys.SetDefault(kSwitchHeaderSourceCommand,
						{.Primary = Core::Chord::Key(ImGuiMod_Alt | ImGuiKey_O)});
	}

	void DocumentWindows::OpenFind()
	{
		if (Core::OpenItem* item = FindActiveTextItem())
		{
			m_FindBar.Open(*item);
		}
	}

	void DocumentWindows::GoToDefinition()
	{
		if (const std::optional<std::size_t> index = FindActiveTextIndex())
		{
			RequestDefinition(*index);
		}
	}

	void DocumentWindows::FindReferences(ReferencesPanel& panel)
	{
		if (const std::optional<std::size_t> index = FindActiveTextIndex())
		{
			RequestReferences(*index, panel);
		}
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

		return item != nullptr && Graphite::CanRun(action, *item, hasText);
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

		return item != nullptr && item->Document != nullptr;
	}

	// WARNING: Any tab closes, whatever it shows: a picture, a failed read or a read still running holds no document.
	bool DocumentWindows::CanClose()
	{
		return m_Documents.GetActiveIndex().has_value();
	}

	void DocumentWindows::RequestCloseOfActiveTab()
	{
		if (const std::optional<std::size_t> active = m_Documents.GetActiveIndex())
		{
			m_CloseRequested = m_Documents.At(*active).Path;
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

		const Core::OpenItem& item = m_Documents.At(index);
		const std::size_t revision = item.Document ? item.Document->GetBuffer().GetRevision() : 0;

		m_Router.CancelDefinition();

		m_Bubble.BeginWaiting(NavigationCommand::SwitchSourceHeader, item.Path, item.Caret.Offset, revision,
							  std::string{kLookingForCounterpart}, ImGui::GetTime());

		m_Router.AskSwitchSourceHeader(item.Path);

		FollowNavigationWait();
	}

	void DocumentWindows::SetTransientMessage(std::string text)
	{
		m_TransientMessage =
			TransientMessage{std::move(text), ImGui::GetTime() + kTransientMessageSeconds};
	}

	// WARNING: A zoom command run while the wheel turns came from Ctrl+wheel over a text editor, since a picture takes
	// the wheel itself and never routes it; the font is what the pointer is over, whichever tab has the focus.
	bool DocumentWindows::ZoomFocusedPicture(PictureZoom zoom)
	{
		if (ImGui::GetIO().MouseWheel != 0.0f)
		{
			return false;
		}

		const std::optional<std::filesystem::path> picture = PictureTakingTheZoom();

		if (!picture)
		{
			return false;
		}

		const std::optional<std::size_t> index = m_Documents.Find(*picture);

		if (!index || !ShowsPicture(m_Documents.At(*index)))
		{
			return false;
		}

		if (const auto shown = m_ShownPictures.find(*picture); shown != m_ShownPictures.end())
		{
			ZoomImageTab(m_Images.For(*picture), zoom, shown->second.Area);
			Core::Redraw::Request();
		}

		return true;
	}

	// WARNING: With the focus outside every document tab, as in a menu or a popup, the zoom is meant for the active tab:
	// a menu takes the focus before its item runs, so no tab holds it at that moment.
	std::optional<std::filesystem::path> DocumentWindows::PictureTakingTheZoom() const
	{
		if (m_FocusedPicture)
		{
			return m_FocusedPicture;
		}

		const std::filesystem::path* const active = m_Documents.GetActiveItemPath();

		if (m_DocumentFocused || active == nullptr || !m_ShownPictures.contains(*active))
		{
			return std::nullopt;
		}

		return *active;
	}

	std::vector<ViewStatusItem> DocumentWindows::GetViewStatusItems() const
	{
		const std::filesystem::path* const active = m_Documents.GetActiveItemPath();

		if (active == nullptr)
		{
			return {};
		}

		const auto shown = m_ShownPictures.find(*active);
		return shown != m_ShownPictures.end() ? shown->second.Status : std::vector<ViewStatusItem>{};
	}

	DocumentWindows::ShownPicture DocumentWindows::RenderImageTab(const CommandContext& context,
																  const Core::OpenItem& item, EditorSettings& settings,
																  const std::filesystem::path& workspaceRoot,
																  bool updatedAsShown)
	{
		// WARNING: A popup does not count: with the zoom menu open, a stroke belongs to the menu, not to the tab behind it.
		const bool holdsKeyboard =
			ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);

		const ImageTabFrame frame = DrawImageTab(m_Images.For(item.Path),
												 BuildBreadcrumb(workspaceRoot, item.Path, {}, 0), settings);

		// A picture takes no keys itself, so what the router leaves is dropped.
		if (holdsKeyboard && context.Input != nullptr)
		{
			bool ranRepeat = false;
			static_cast<void>(RouteFocusedEditorStrokes(context, *context.Input, ranRepeat));
		}

		// WARNING: A tab that came on screen this frame was updated as hidden, so its load starts on the next frame,
		// which nothing else may ask for.
		if (!updatedAsShown)
		{
			Core::Redraw::Request();
		}

		return ShownPicture{.Status = frame.Status, .Area = frame.Area, .Focused = frame.Focused};
	}

	void DocumentWindows::CheckPictureOnDisk(std::size_t index)
	{
		if (const Core::OpenItem& item = m_Documents.At(index); ShowsPicture(item))
		{
			m_Images.CheckDisk(item.Path);
		}
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
		ImGui::ClearWindowSettings(DocumentWindowName(m_Documents, index).c_str());
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
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const LargeFileQuestion* asked = m_SizeQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const LargeFileQuestion question = *asked;

		if (question.Size == 0)
		{
			DialogLayout::Text("This file can no longer be read. Load it anyway?");
		}
		else
		{
			DialogLayout::Text(std::format("This file is {}. Load it anyway?", Core::FormatWholeMegabytes(question.Size)));
		}

		DialogLayout::Gap();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow({.Label = "Load"});

		if (choice == DialogLayout::Choice::Safe)
		{
			LoadConfirmed(question.Path, question.Permanent);
			m_SizeQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_SizeQuestion.Answer();
		}

		m_SizeQuestion.End();
	}

	void DocumentWindows::RenderReloadConflict(const ImVec4& danger)
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const std::filesystem::path* asked = m_ReloadConflict.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const std::filesystem::path path = *asked;

		DialogLayout::Text(std::format("{} changed on disk, and this tab has unsaved changes.", FileNameOf(path)));
		DialogLayout::Gap();
		DialogLayout::MutedText("Reloading loses the unsaved changes.");
		DialogLayout::Gap();

		// WARNING: Keeping your version is the Cancel slot, so Escape keeps it rather than leaving the
		// conflict unanswered.
		const DialogLayout::ButtonSpec absentButton{};
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(absentButton, {.Label = "Reload from Disk"}, danger, absentButton, "Keep My Version");

		if (choice == DialogLayout::Choice::Destructive)
		{
			if (const std::optional<std::size_t> index = m_Documents.Find(path))
			{
				ReloadOne(*index);
			}

			m_ReloadConflict.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			if (const std::optional<std::size_t> index = m_Documents.Find(path); index && m_Documents.At(*index).Document)
			{
				m_Documents.At(*index).Document->AcceptDiskState();
			}

			m_ReloadConflict.Answer();
		}

		m_ReloadConflict.End();
	}

	void DocumentWindows::RenderOverwriteConfirm(const ImVec4& danger)
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const OverwriteQuestion* asked = m_OverwriteConfirm.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const OverwriteQuestion question = *asked;

		DialogLayout::Text(std::format("{} changed on disk since it was opened. Overwrite it?", FileNameOf(question.Path)));
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec absentButton{};
		const DialogLayout::Choice choice = DialogLayout::ButtonRow(absentButton, {.Label = "Overwrite"}, danger);

		if (choice == DialogLayout::Choice::Destructive)
		{
			const std::optional<std::size_t> index = m_Documents.Find(question.Path);
			const bool saved = index && SaveOne(*index);

			if (saved && question.ThenClose)
			{
				CloseIfOpen(question.Path);
			}

			m_OverwriteConfirm.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
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

		if (item.Document && item.Document->IsModified() && Core::IsLastHolder(item.Document))
		{
			return true;
		}

		CloseAndForgetLayout(*index);

		return false;
	}

	void DocumentWindows::RenderCloseConfirm(const ImVec4& danger)
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const std::filesystem::path* asked = m_CloseConfirm.Begin(
			[this](const std::filesystem::path& candidate) { return StillNeedsCloseConfirmation(candidate); });
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const std::filesystem::path path = *asked;

		DialogLayout::Text(std::format("Save changes to {} before closing?", FileNameOf(path)));
		DialogLayout::Gap();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow({.Label = "Save"}, {.Label = "Discard"}, danger);

		if (choice == DialogLayout::Choice::Safe)
		{
			const std::optional<std::size_t> index = m_Documents.Find(path);
			const bool changedOnDisk =
				index && m_Documents.At(*index).Document && m_Documents.At(*index).Document->CheckChangedOnDisk();

			// WARNING: Enter answers Save, so a file another program has changed is asked about, as any other
			// save asks, rather than written over by one key.
			if (changedOnDisk)
			{
				m_CloseConfirm.Answer();
				m_OverwriteConfirm.AskOnce(OverwriteQuestion{.Path = path, .ThenClose = true});
			}
			else if (!index || SaveOne(*index))
			{
				CloseIfOpen(path);
				m_CloseConfirm.Answer();
			}
		}
		else if (choice == DialogLayout::Choice::Destructive)
		{
			CloseIfOpen(path);
			m_CloseConfirm.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
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
		m_ActiveEditor.Set(*this);

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

			m_DocumentStates.OpenWithServer(item.Path, *item.Document);

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

			if (const std::optional<LensState>& lens = closedState.Lens; lens && LearnsFromServer(path))
			{
				KeepAnalysis(path, *lens, m_DocumentStates.SemanticFor(path));
			}

			m_States.erase(path);
		}

		m_DocumentStates.Prune(m_Documents.GetStore());

		std::set<std::filesystem::path> openPaths;

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			openPaths.insert(m_Documents.At(index).Path);
		}

		m_Images.Prune(openPaths);

		m_ChangeMarkers.Retain(
			[this](const std::filesystem::path& path) { return m_Documents.GetStore().Find(path) != nullptr; });
	}

	bool DocumentWindows::IsAnalysing(const std::filesystem::path& path) const
	{
		return m_Router.IsAnalysing(path);
	}

	// WARNING: Under a server turned off, never chosen or kept from an untrusted folder, a file's lenses are hidden and
	// keep only what an earlier session learned: restoring its counts would show nothing, and keeping them on close
	// would write that session's analysis over a better one.
	bool DocumentWindows::LearnsFromServer(const std::filesystem::path& path)
	{
		return !HasNoServerToWaitFor(m_Router.StatusOf(path));
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

		if (LearnsFromServer(item.Path))
		{
			lens.Model.SeedCounts(std::move(restored->Counts));
		}

		// WARNING: A lens anchors to a row and this runs before the document has drawn once, so
		// its row map is empty and every line maps to row zero. Anchoring now would collapse the
		// restored declarations onto one row; the rebuild a frame later anchors them properly.
		lens.NeedsRebuild = true;

		m_DocumentStates.SemanticFor(item.Path) = std::move(restored->Semantic);
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

	void DocumentWindows::DropInFlightCounts(const std::filesystem::path& path, LensState& state)
	{
		state.Model.ForgetAsked();

		m_Router.DropCounts(path);
	}

	void DocumentWindows::AnnounceReload(const Core::OpenItem& item)
	{
		if (!item.Document)
		{
			return;
		}

		// The reload reaches the server now, before this frame asks it anything. A document this
		// class opened goes through DocumentStates, which records what it sent so nothing resends
		// it later; any other copy the server holds, opened by a preview or the background
		// analysis, is updated directly.
		if (m_DocumentStates.IsOpenWithServer(item.Path))
		{
			m_DocumentStates.SendChangeIfMoved(item.Path, *item.Document, SendTiming::Now);
		}
		else
		{
			m_Server.ChangeDocument(item.Path, LanguageOfPath(item.Path), item.Document->GetBuffer().Materialize());
		}

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

		for (const std::size_t row : state.Model.TakeRowsToAsk(firstRowWanted, endRowWanted))
		{
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
			state.Model.ForgetAsked();
		}

		// WARNING: A count that failed is not asked again on its own, so one that failed while the server was busy
		// would say unavailable until the next edit; the server becoming able to count again is when to retry it.
		const bool serverCanCount = m_Router.CanCountReferences(item.Path);

		if (serverCanCount && !state.ServerCouldCount)
		{
			state.Model.ForgetFailed();
		}

		state.ServerCouldCount = serverCanCount;

		if (m_DocumentStates.SendChangeIfMoved(item.Path, *item.Document))
		{
			DropInFlightCounts(item.Path, state);
		}

		if (state.HaveDeclarations && (state.NeedsRebuild || state.BuiltAt != item.ColumnsPerWrappedRow))
		{
			state.Model.SetDeclarations(state.Declarations, item.Rows,
										item.Document->GetBuffer(), item.TabWidth);
			state.BuiltAt = item.ColumnsPerWrappedRow;
			state.NeedsRebuild = false;
			state.Model.ForgetAsked();
		}

		m_Router.AskSymbolsIfDue(item.Path);
		AskForVisibleCounts(item, state, frame);

		state.Model.SetWaitingText(LensWaitingText(m_Router.StatusOf(item.Path)));
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

		return true;
	}

	void DocumentWindows::ApplySemanticAnswer(const std::filesystem::path& path,
											  const LanguageServices::Answer& answer)
	{
		const std::shared_ptr<Core::TextDocument> document = m_Documents.GetStore().Find(path);

		if (!document)
		{
			return;
		}

		const std::optional<std::size_t> index = m_Documents.Find(path);
		const std::size_t tabWidth = index ? m_Documents.At(*index).TabWidth : Core::DefaultTabWidth;

		m_DocumentStates.SemanticFor(path).Set(answer.SemanticTokens, document->GetBuffer(), tabWidth);
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

		// WARNING: F12 and Alt+O share one bubble, so the command it shows replaces the other's question.
		m_Router.CancelSwitchSourceHeader();

		m_Bubble.BeginWaiting(NavigationCommand::Definition, item.Path, item.Caret.Offset, buffer.GetRevision(),
							  std::string{kFindingDefinition}, ImGui::GetTime());

		m_Router.AskDefinition(DefinitionQuestion{.Path = item.Path,
												  .CaretOffset = item.Caret.Offset,
												  .From = where,
												  .Screen = screenPosition});

		FollowNavigationWait();
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

		RequestReferencesAt(item, where, symbol, panel);
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

			RequestReferencesAt(item, where, declaration.Name, panel);
			return;
		}
	}

	void DocumentWindows::RequestReferencesAt(const Core::OpenItem& item, const FilePosition& where,
											  const std::string& symbol, ReferencesPanel& panel)
	{
		AskReferencesAndShow(ReferencesQuestion{.Path = item.Path, .Where = where, .Symbol = symbol}, panel);
		panel.Focus();
	}

	void DocumentWindows::AskReferencesAndShow(ReferencesQuestion question, ReferencesPanel& panel)
	{
		const std::string symbol = question.Symbol;
		const std::filesystem::path path = question.Path;

		if (m_Router.AskReferences(std::move(question)) == QuestionFate::HeldUntilServerIsUp)
		{
			panel.ShowWaiting(symbol, ServerWaitingText(m_Router.StatusOf(path)));
			return;
		}

		panel.ShowWorking(symbol);
	}

	void DocumentWindows::FollowReferencesPanel(ReferencesPanel& panel)
	{
		if (const ReferencesQuestion* held = m_Router.HeldReferences())
		{
			panel.ShowWaiting(held->Symbol, ServerWaitingText(m_Router.StatusOf(held->Path)));
		}

		if (panel.TakeRetry() && m_Router.LastReferences())
		{
			AskReferencesAndShow(*m_Router.LastReferences(), panel);
		}
	}

	void DocumentWindows::ShowReferencesAnswer(const ReferencesQuestion& question, const LanguageServices::Answer& answer,
											   ReferencesPanel& panel)
	{
		if (answer.Failed && answer.FailedBecause == ReplyFailure::ServerUnavailable)
		{
			panel.ShowUnavailable(question.Symbol, AsSentence(ReplyFailureText(answer), answer.ServerAtFailure));
			return;
		}

		if (answer.Failed)
		{
			panel.ShowFailure(question.Symbol, ReplyFailureText(answer));
			return;
		}

		const ServerStatus status = m_Router.StatusOf(question.Path);
		const std::string incompleteWhy =
			IsStillIndexing(status) ? StillIndexingText(status) + ", the list may be incomplete" : std::string{};

		panel.ShowResults(question.Symbol, answer.Locations, incompleteWhy);
	}

	void DocumentWindows::ShowReferencesRefreshing(const ReferencesQuestion& question, ReferencesPanel& panel)
	{
		panel.ShowRefreshing("Updating the list now that " + ServerNameOf(m_Router.StatusOf(question.Path)) +
							 " has finished indexing");
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
		if (answer.Failed)
		{
			m_Bubble.End(NavigationCommand::SwitchSourceHeader, AsSentence(ReplyFailureText(answer), answer.ServerAtFailure), ImGui::GetTime());
			return;
		}

		if (answer.Locations.empty())
		{
			APP_INFO("No header/source counterpart for {}", Core::ToUtf8(from));
			m_Bubble.End(NavigationCommand::SwitchSourceHeader, std::string{kNoCounterpart}, ImGui::GetTime());
			return;
		}

		m_Bubble.Close(NavigationCommand::SwitchSourceHeader);

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
			m_Bubble.End(NavigationCommand::Definition, AsSentence(ReplyFailureText(answer), answer.ServerAtFailure), ImGui::GetTime());
			return;
		}

		std::vector<Location> candidates;
		const DefinitionOutcome outcome = ResolveDefinition(answer.Locations, question.From, candidates);

		switch (outcome)
		{
		case DefinitionOutcome::None:
			m_Bubble.End(NavigationCommand::Definition, NoDefinitionFoundText(m_Router.StatusOf(question.Path)), ImGui::GetTime());
			break;

		case DefinitionOutcome::Jumped:
			m_Bubble.Close(NavigationCommand::Definition);

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
				m_Bubble.End(NavigationCommand::Definition, std::string{kCloseTheDialogFirst}, ImGui::GetTime());
				break;
			}

			m_Bubble.Close(NavigationCommand::Definition);

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
				Core::PushCodeFont();

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

				ImGui::PopFont();
				ImGui::Unindent();
			}

			ImGui::PopID();
		}

		ImGui::EndPopup();
	}

	void DocumentWindows::FollowNavigationWait()
	{
		const ServerStatus status = m_Router.StatusOf(m_Bubble.GetPath());
		const std::string waitingForServer = AsSentence(ServerWaitingText(status), status);

		if (m_Bubble.IsWaitingFor(NavigationCommand::Definition))
		{
			m_Bubble.SetWaitingText(m_Router.HoldsDefinition() ? waitingForServer : std::string{kFindingDefinition});
		}

		if (m_Bubble.IsWaitingFor(NavigationCommand::SwitchSourceHeader))
		{
			m_Bubble.SetWaitingText(m_Router.HoldsSwitchSourceHeader() ? waitingForServer
																	   : std::string{kLookingForCounterpart});
		}
	}

	void DocumentWindows::AbandonNavigationIfReaderMovedOn()
	{
		if (!m_Bubble.IsOpen())
		{
			return;
		}

		ReaderPosition reader{.ActiveDocument = ActiveTextDocument(m_Documents),
							  .BubbleDocumentShown = IsShown(m_Bubble.GetPath()),
							  .EscapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, kNotRepeating)};

		if (const std::optional<std::size_t> index = m_Documents.Find(m_Bubble.GetPath()); index && m_Documents.At(*index).Document)
		{
			const Core::OpenItem& item = m_Documents.At(*index);

			reader.CaretOffset = item.Caret.Offset;
			reader.Revision = item.Document->GetBuffer().GetRevision();
		}
		else
		{
			reader.BubbleDocumentShown = false;
		}

		const std::optional<NavigationCommand> abandoned = m_Bubble.AbandonIfReaderMovedOn(reader);

		if (abandoned == NavigationCommand::Definition)
		{
			m_Router.CancelDefinition();
		}
		else if (abandoned == NavigationCommand::SwitchSourceHeader)
		{
			m_Router.CancelSwitchSourceHeader();
		}
	}

	void DocumentWindows::RenderNavigationBubble()
	{
		const double now = ImGui::GetTime();

		if (const std::optional<double> changeAt = m_Bubble.NextChangeAfter(now))
		{
			Core::Redraw::RequestIn(std::chrono::duration<double>(*changeAt - now));
		}

		const std::optional<ShownBubble> shown = m_Bubble.ShownAt(now);
		const DocumentState* const state = FindState(m_Bubble.GetPath());

		const bool caretDrawnThisFrame = state != nullptr && state->Caret.DrawnInFrame == ImGui::GetFrameCount();

		if (!shown || !caretDrawnThisFrame)
		{
			return;
		}

		m_BubbleSize = DrawNavigationBubble(*shown, state->Caret, m_BubbleSize);
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

			void ApplyReferences(const ReferencesQuestion& question,
								 const LanguageServices::Answer& answer) override
			{
				m_Windows.ShowReferencesAnswer(question, answer, m_Panel);
			}

			void AskedHeldReferences(const ReferencesQuestion& question) override
			{
				m_Panel.ShowWorking(question.Symbol);
			}

			void RefreshingReferences(const ReferencesQuestion& question) override
			{
				m_Windows.ShowReferencesRefreshing(question, m_Panel);
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

			void FailCount(const std::filesystem::path& path, std::size_t documentRow) override
			{
				if (LensState* const lens = m_Windows.FindLens(path))
				{
					lens->Model.SetCountFailed(documentRow);
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
		bool highlightOccurrences, HoverSettings hoverSettings, const ResolvedScheme& scheme,
		ReferencesPanel& referencesPanel,
		Core::TabCloseRequests& closeRequests, const std::filesystem::path& workspaceRoot, EditorSettings& settings)
	{
		m_Shown.clear();

		// WARNING: Every picture is updated before any is drawn, because a load that landed or a gif's next frame
		// frees the texture shown before it. Which tabs are on screen is known only as their windows begin, so the
		// pictures are updated as last frame showed them.
		std::set<std::filesystem::path> updatedAsShown;

		for (const auto& [path, picture] : m_ShownPictures)
		{
			updatedAsShown.insert(path);
		}

		m_Images.Update(std::chrono::steady_clock::now(), updatedAsShown);

		std::map<std::filesystem::path, ShownPicture> shownPictures;
		std::optional<std::filesystem::path> focusedPicture;
		bool documentFocused = false;

		for (std::size_t index = 0; index < m_Documents.GetCount();)
		{
			Core::OpenItem& item = m_Documents.At(index);
			const std::string identity = DocumentIdentityOf(m_Documents, index);
			const std::string name = DocumentWindowName(m_Documents, index);

			const bool appearing = IsNewSinceLastFrame(identity);
			const bool noPendingFocus = false;

			TabWindowState window{.Fresh = appearing,
								  .Restored = appearing && m_RestoredIdentities.erase(identity) > 0,
								  .Focus = std::exchange(item.PendingFocus, noPendingFocus)};

			const PlaceNewTab place = [this, &identity, fallbackDockNode](const std::string& windowName) {
				DockInto(ChooseDockTarget(identity, fallbackDockNode))(windowName);
			};

			const bool preview = item.Preview;

			Core::PushContentWindowColours();

			const GitDecoration decoration =
				item.Path.empty() ? GitDecoration::None : m_GitDecorations.FileOf(item.Path);
			const FileTab tab{.Path = item.Path,
							  .Title = m_Documents.DisplayTitle(index),
							  .Preview = preview,
							  .Dot = TabDotColour(decoration, m_GitDecorations.ColourFor(decoration))};

			const bool modified = item.Document && item.Document->IsModified();
			const ImGuiWindowFlags windowFlags =
				modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;

			const TabWindowFrame frame = BeginTabWindow(name, window, place, closeRequests, windowFlags,
														FileTabLook(tab, Core::ImGuiRenderer::GetItalicFont()));
			const bool visible = frame.Visible;
			bool open = frame.Open;

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

				// Another view may have edited this document since the tab last drew.
				Core::CatchUpWithDocument(item, Core::CaretFollow::Shift);

				// WARNING: Child windows count, because a click on the text focuses the editor
				// surface, a child of this window. Without the flag the right half of a split stayed
				// active and commands acted on the half nobody was looking at.
				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
				{
					ActivateAndRememberNode(index);
					documentFocused = true;
				}

				const ImGuiMouseButton leftMouseButton = ImGuiMouseButton_Left;

				if (preview && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(leftMouseButton))
				{
					m_Documents.Promote(index);
				}
			}

			if (visible && ShowsPicture(item))
			{
				ShownPicture picture =
					RenderImageTab(context, item, settings, workspaceRoot, updatedAsShown.contains(item.Path));

				if (picture.Focused)
				{
					focusedPicture = item.Path;
				}

				shownPictures.insert_or_assign(item.Path, std::move(picture));
			}
			else if (visible)
			{
				LensState* const lens = LensFor(item);
				DocumentState& state = StateFor(item.Path);

				EditorView view;
				view.FontSize = fontSize;
				view.Indent = indentStyle;
				view.WordWrap = wordWrap;
				view.ChangeDisplay = changeDisplay;
				view.HighlightOccurrences = highlightOccurrences;
				view.Scheme = &scheme;
				view.Lens = lens != nullptr ? &lens->Model : nullptr;
				view.Declarations = lens != nullptr ? &lens->Declarations : nullptr;

				if (item.Document)
				{
					// Only a tab with a document gets an entry: Prune would drop one made for a tab
					// without, and the next frame would build it again.
					view.Syntax = &m_DocumentStates.SyntaxFor(item.Path, item.TabWidth);
					view.Semantic = &m_DocumentStates.SemanticFor(item.Path);

					DiagnosticOverlay& diagnostics = m_DocumentStates.DiagnosticFor(
						item.Path, item.Document->GetBuffer(), item.TabWidth);

					if (!diagnostics.IsEmpty())
					{
						view.Diagnostics = &diagnostics;
					}
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

				if (lens != nullptr)
				{
					const ServerStatus serverStatus = m_Router.StatusOf(item.Path);

					view.HoverLoadingText = HoverLoadingText(serverStatus);
					view.ShowLensRows = !HasNoServerToWaitFor(serverStatus);
				}

				if (state.Hover.Preview)
				{
					const std::optional<std::size_t> shownIndex = m_Documents.Find(state.Hover.Preview->Path);

					if (shownIndex)
					{
						view.HoverPreviewOverlay = &m_DocumentStates.SemanticFor(state.Hover.Preview->Path);
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

				if (item.Document)
				{
					const std::size_t caretLine = item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line;
					const std::span<const Declaration> declarations =
						lens != nullptr ? std::span<const Declaration>{lens->Declarations} : std::span<const Declaration>{};
					DrawBreadcrumb(BuildBreadcrumb(workspaceRoot, item.Path, declarations, caretLine));
				}

				const EditorFrameResult editorResult = DrawEditor(context, item, view, m_FindBar);

				if (pendingTop && editorResult.DrewRows && pendingTop->Settle(*view.Anchor))
				{
					pendingTop.reset();
				}

				state.CaretScreen =
					CaretScreenPosition{.X = editorResult.CaretScreenX, .Y = editorResult.CaretScreenY};

				state.Caret = CaretPlacement{.X = editorResult.CaretScreenX,
											 .Y = editorResult.CaretScreenY,
											 .LineHeight = editorResult.CaretLineHeight,
											 .SurfaceMin = ImVec2(editorResult.SurfaceLeft, editorResult.SurfaceTop),
											 .SurfaceMax = ImVec2(editorResult.SurfaceRight, editorResult.SurfaceBottom),
											 .Viewport = editorResult.Viewport,
											 .DrawnInFrame = ImGui::GetFrameCount()};

				if (lens != nullptr)
				{
					UpdateLenses(item, *lens, editorResult);
				}

				if (editorResult.Edited && preview)
				{
					m_Documents.Promote(index);
				}

				if (editorResult.Wrap)
				{
					m_WrapOfLastDrawnEditor = editorResult.Wrap;
				}

				if (editorResult.Wrap && Core::IsLoading(item))
				{
					state.PaneWrapWhileRead = editorResult.Wrap;
				}

				if (editorResult.RetryPressed)
				{
					RetryLoad(index);
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

			// WARNING: Checked again after the tab draws: a chord bound to the close command runs
			// from inside its editor's or its picture's draw, after the check above had already looked.
			if (m_CloseRequested && *m_CloseRequested == item.Path)
			{
				m_CloseRequested.reset();
				open = false;
			}

			ImGui::End();
			// WARNING: The pop waits for End: the editor surface and every child begun inside the
			// window read ChildBg while it is open.
			Core::PopContentWindowColours();

			if (!open)
			{
				const bool asksBeforeClosing = modified && Core::IsLastHolder(item.Document);

				if (asksBeforeClosing)
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

		m_ShownPictures = std::move(shownPictures);
		m_FocusedPicture = std::move(focusedPicture);
		m_DocumentFocused = documentFocused;

		// Edits a drawn tab made were sent as it drew. This sends the rest: a reload, or an edit
		// made through another view.
		for (const std::filesystem::path& path : m_DocumentStates.SendChanges(m_Documents.GetStore()))
		{
			if (LensState* const lens = FindLens(path))
			{
				DropInFlightCounts(path, *lens);
			}
		}
	}

	void DocumentWindows::Render(const CommandContext& context, const EditorFrame& frame,
								 ReferencesPanel& referencesPanel)
	{
		m_WorkspaceRoot = frame.WorkspaceRoot;

		if (frame.Indent.Width > 0)
		{
			m_Documents.SetTabWidthForLoads(frame.Indent.Width);
		}

		LandFinishedLoads();

		// WARNING: The order is load-bearing. Answers come first so they are on screen this frame,
		// and popups open before any Begin. A renamed document's pending dock target must be
		// consumed by the Begin in RenderDocumentWindows before its old identity's node is
		// undocked, so ForgetLayoutOfWindowsClosedElsewhere runs after the loop.
		const std::vector<std::string> startingIdentities = CurrentIdentities(m_Documents);

		CollectServerAnswers(referencesPanel);
		FollowReferencesPanel(referencesPanel);
		FollowNavigationWait();

		const ImVec4 danger = DangerColourOf(frame.Scheme);

		RenderSizeQuestion();
		RenderReloadConflict(danger);
		RenderOverwriteConfirm(danger);
		RenderCloseConfirm(danger);
		RenderDefinitionChoices(frame.Scheme, frame.Indent.Width);

		RenderDocumentWindows(context, frame.FontSize, frame.Indent, frame.FallbackDockNode,
							  frame.WordWrap, frame.ChangeMarkers, frame.HighlightOccurrences, frame.Hover, frame.Scheme,
							  referencesPanel,
							  frame.CloseRequests, frame.WorkspaceRoot, frame.Settings);

		HandWrapToLoads(frame.WordWrap);

		m_CloseRequested.reset();

		AbandonNavigationIfReaderMovedOn();
		RenderNavigationBubble();

		PruneDocumentStates();

		ForgetLayoutOfWindowsClosedElsewhere(startingIdentities);

		m_PreviousIdentities = CurrentIdentities(m_Documents);
	}
}

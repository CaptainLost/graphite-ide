#pragma once

#include "UI/Dialogs/ModalGate.h"
#include "Editor/Documents/DocumentStates.h"
#include "Editor/ActiveTextDocument.h"
#include "Editor/EditorCommandTarget.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/EditorWidgetState.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Navigation/CandidateLine.h"
#include "Editor/Navigation/NavigationBubble.h"
#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/ExpectedWrites.h"
#include "Editor/Overlays/LensModel.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Editor/ScrollAnchor.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Editor/StatusBar/StatusBar.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Editor/TextEditorWidget.h"

#include "Editor/Authors/LinePeekContent.h"

#include "Hex/HexView.h"
#include "Image/Zoom.h"

#include "Git/Blame/BlameModel.h"
#include "Git/Changes/ChangeMarkersModel.h"
#include "Git/GitDecorationView.h"

#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Syntax/SyntaxCache.h"

#include "Search/ReferencesPanel.h"
#include "Persistence/PersistedState.h"

#include "Core/Base/Timestep.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/ToastStack.h"
#include "Core/Platform/AsyncDialog.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Core {
	class CommandRegistry;
	class DocumentSet;
	class EventBus;
	class Keymap;
}

namespace Graphite {
	class EditorSettings;
	class HexFiles;
	class ImageTabs;

	struct EditorFrame
	{
		float FontSize;
		Core::IndentStyle Indent;
		unsigned int FallbackDockNode;
		bool WordWrap;
		ChangeMarkerDisplay ChangeMarkers;
		bool HighlightOccurrences;
		bool LineAuthor;
		int LineAuthorDelayMs = 0;   // how long the caret rests on a line before its author shows
		HoverSettings Hover;
		const ResolvedScheme& Scheme;
		Core::TabCloseRequests& CloseRequests;
		const std::filesystem::path& WorkspaceRoot;
		EditorSettings& Settings;
	};

	class DocumentWindows final : public EditorCommandTarget
	{
	public:
		DocumentWindows(Core::DocumentSet& documents, ModalGate& modals,
						DocumentServerPort& server, const DiagnosticsModel& diagnostics,
						ChangeMarkersModel& changeMarkers, BlameModel& blame, PreviewSemantics& previewSemantics,
						AnalysisCache& analysis, AnalysisStore& analysisStore, WorkspaceAnalysis& workspaceAnalysis,
						Core::EventBus& events, ActiveEditorTarget& activeEditor, ImageTabs& images, HexFiles& hex)
			: m_Documents(documents),
			  m_ActiveEditor(activeEditor),
			  m_Images(images),
			  m_Hex(hex),
			  m_Events(events),
			  m_Server(server),
			  m_Diagnostics(diagnostics),
			  m_ChangeMarkers(changeMarkers),
			  m_Blame(blame),
			  m_PreviewSemantics(previewSemantics),
			  m_Analysis(analysis),
			  m_AnalysisStore(analysisStore),
			  m_WorkspaceAnalysis(workspaceAnalysis),
			  m_Modals(modals),
			  m_SizeQuestion(modals, kSizeQuestionTitle),
			  m_ReloadConflict(modals, kReloadConflictTitle),
			  m_OverwriteConfirm(modals, kOverwriteConfirmTitle),
			  m_CloseConfirm(modals, kCloseConfirmTitle),
			  m_GoToLine(modals, kGoToLineTitle),
			  m_Router(server, &DocumentWindows::FrameClock)
		{
		}

		void Render(const CommandContext& context, const EditorFrame& frame,
					ReferencesPanel& referencesPanel);

		void RequestOpen(const std::filesystem::path& path, bool permanent);

		// A permanent tab opened without the large-file question: the unsaved changes put back into its document are
		// reason enough to read it.
		void OpenPutBack(const std::filesystem::path& path) { LoadConfirmed(path, true); }

		void LandFinishedLoads();

		void SetGitDecorations(const GitDecorationView& decorations) { m_GitDecorations = decorations; }

		WorkspaceState CaptureState() const;

		void RestoreState(const WorkspaceState& state);

		void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to);

		void HandleFilesChanged(const std::vector<std::filesystem::path>& directories,
								bool everything);

		void NotifyWindowFocus(bool focusedNow);

		bool SaveAllModified();

		bool HasModifiedDocuments() const;

		bool SaveDocuments(const std::vector<std::filesystem::path>& paths);

		void RevertDocuments(const std::vector<std::filesystem::path>& paths);

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		bool CanRun(EditorAction action) override { return CanRunOnActiveDocument(action); }
		void Queue(EditorAction action, bool fromMenu) override { QueueOnActiveDocument(action, fromMenu); }
		bool CanSave() override { return ActiveTabHoldsDocument(); }
		void Save() override { RequestSaveOfActiveDocument(); }
		bool CanClose() override;
		void Close() override { RequestCloseOfActiveTab(); }
		bool CanFind() override { return FindActiveTextItem() != nullptr; }
		void OpenFind() override;
		std::optional<std::filesystem::path> CaretFile() override { return ActiveTextDocument(m_Documents); }
		void GoToDefinition() override;
		void FindReferences(ReferencesPanel& panel) override;
		bool CanShowLineHistory() override;
		void ShowLineHistory(bool fromMenu) override;

		std::optional<std::string> GetStatusMessage() const;

		// True when a picture takes the zoom instead of the font: the one in the tab with the keyboard focus last
		// frame, or, when no tab had it, the active tab's.
		bool ZoomFocusedPicture(PictureZoom zoom);

		// What the active tab's picture or bytes put in the status bar last frame; nothing when it shows neither.
		std::vector<ViewStatusItem> GetViewStatusItems() const;

		void SetTransientMessage(std::string text);

		void RequestDefinition(std::size_t index);

		void QueueEditorAction(std::size_t index, EditorAction action, bool fromMenu = false);

		void RequestSwitchSourceHeader(std::size_t index);

		void RequestReferences(std::size_t index, ReferencesPanel& panel);

		void ExpectExternalWrite(const std::vector<std::filesystem::path>& paths)
		{
			m_ExpectedWrites.Expect(paths);
		}

		void TickExpectedWrites(Core::Timestep timestep) { m_ExpectedWrites.Tick(timestep); }

		void ClearExpectedWrites() { m_ExpectedWrites.Clear(); }

	private:
		struct LensState
		{
			LensModel Model;

			std::vector<Declaration> Declarations;

			std::size_t BuiltAt = 0;

			bool HaveDeclarations = false;

			bool WasModified = false;

			bool NeedsRebuild = false;

			bool ServerCouldCount = false;

			// The sum of every document's diagnostics generation when this lens last looked: it moves whenever the server
			// has analysed any file again, which is when a count it gave may have changed.
			std::uint64_t SeenAnalyses = 0;

			// FrameClock() when the counts are to be asked again, once the analyses have stopped moving for a moment.
			std::optional<double> RecountAt;
		};

		struct LinePeekState
		{
			std::size_t Line = 0;
			std::size_t Selected = 0;

			// The authors table the line's history was last asked from. A table a few edits old can name another HEAD
			// line or none, so the history is asked again from each newer table; the blame model ignores a request equal
			// to the one it holds.
			Core::Ref<const LineAuthors> AskedFrom;

			// Passed to the widget on the next frame the tab draws, so the peek is scrolled into view once.
			bool JustOpened = true;

			// The buffer's change sequence when the peek opened: an edit made through another view of the document
			// closes the peek as an edit through this one does.
			std::uint64_t ChangeSequence = 0;

			// What the peek shows, built again only when the authors, the history, the line or the entry changes.
			LinePeekCache Cache;
		};

		struct DocumentState;

		DocumentState& StateFor(const std::filesystem::path& path);

		DocumentState* FindState(const std::filesystem::path& path);

		const DocumentState* FindState(const std::filesystem::path& path) const;

		LensState* FindLens(const std::filesystem::path& path);

	public:
		bool IsAnalysing(const std::filesystem::path& path) const;

		bool IsShown(const std::filesystem::path& path) const;

	private:
		LensState* LensFor(const Core::OpenItem& item);
		void KeepAnalysis(const std::filesystem::path& path, const LensState& lens,
						  const SemanticOverlay& semantic);

		void RestoreAnalysis(const Core::OpenItem& item, DocumentState& state);

		bool LearnsFromServer(const std::filesystem::path& path);

		static std::optional<std::filesystem::file_time_type> ModifiedTimeOf(
			const std::filesystem::path& path);

		void PruneDocumentStates();

		// Opens the peek under `line`, in place of any other the document has; on the line it is already open on, only
		// scrolls it into view again.
		void OpenLinePeek(const Core::OpenItem& item, std::size_t line);

		// Asks for the line's history from each authors table the blame gives, and gives what the peek shows, held in
		// the peek until what it is built from changes.
		const LinePeekContent& LinePeekContentFor(const Core::OpenItem& item, LinePeekState& peek,
												  const Core::Ref<const LineAuthors>& authors,
												  const std::chrono::time_zone* zone);

		// Closes the peek on the caret leaving its line, an edit or a close request, or follows what was clicked in it.
		void SettleLinePeek(const Core::OpenItem& item, DocumentState& state, const EditorFrameResult& frame,
							const LinePeekContent* content);

		void UpdateLenses(const Core::OpenItem& item, LensState& state,
						  const EditorFrameResult& frame);

		void DropInFlightCounts(const std::filesystem::path& path, LensState& state);

		void AnnounceReload(const Core::OpenItem& item);

		void AskForVisibleCounts(const Core::OpenItem& item, LensState& state,
								 const EditorFrameResult& frame);

		void RequestReferencesAt(const Core::OpenItem& item, const FilePosition& where, const std::string& symbol,
								 ReferencesPanel& panel);

		void AskReferencesAndShow(ReferencesQuestion question, ReferencesPanel& panel);

		void FollowReferencesPanel(ReferencesPanel& panel);

		void ShowReferencesAnswer(const ReferencesQuestion& question, const LanguageServices::Answer& answer,
								  ReferencesPanel& panel);

		void ShowReferencesRefreshing(const ReferencesQuestion& question, ReferencesPanel& panel);

		void RequestReferencesForLens(const Core::OpenItem& item, const LensState& state,
									  std::size_t documentRow, ReferencesPanel& panel);

		void CollectServerAnswers(ReferencesPanel& referencesPanel);

		bool ApplySymbolAnswer(const std::filesystem::path& path,
							   const LanguageServices::Answer& answer);

		void ApplySemanticAnswer(const std::filesystem::path& path,
								 const LanguageServices::Answer& answer);

		void ApplyDefinitionAnswer(const DefinitionQuestion& question,
								   const LanguageServices::Answer& answer);

		void ApplySwitchSourceHeaderAnswer(const std::filesystem::path& from,
										   const LanguageServices::Answer& answer);

		void ApplyHoverAnswer(const HoverQuestion& question,
							  const LanguageServices::Answer& answer);
		void ApplyPreviewAnswer(const HoverQuestion& question,
								const LanguageServices::Answer& answer);

		static double FrameClock();

		void RenderDefinitionChoices(const ResolvedScheme& scheme, std::size_t tabWidth);

		void FollowNavigationWait();

		void AbandonNavigationIfReaderMovedOn();

		void RenderNavigationBubble();

		void SyncEveryDocument();

		struct ReloadBatch
		{
			std::vector<std::size_t> Expected;
			bool AnyFailed = false;
		};

		void RespondToSync(std::size_t index, Core::FileSyncOutcome outcome, ReloadBatch& batch);

		void ReloadExpected(std::size_t index, ReloadBatch& batch);

		void AnnounceExpectedReloads(const ReloadBatch& batch);

		enum class ReloadAnnouncement
		{
			Announce,
			Silent
		};

		bool ReloadOne(std::size_t index, ReloadAnnouncement announcement = ReloadAnnouncement::Announce);

		std::optional<std::size_t> FindOpen(const std::filesystem::path& path) const;

		void AskAboutSize(const std::filesystem::path& path, bool permanent);

		void LoadConfirmed(const std::filesystem::path& path, bool permanent);

		void KeepRestoredTopLine(const Core::OpenItem& item, std::size_t savedTopLine);

		void FinishStateOfRead(std::size_t index);

		void HandWrapToLoads(bool wordWrap);

		void HandWrapOfItsPaneToRead(std::size_t index, bool wordWrap);

		void RetryLoad(std::size_t index);

		bool SaveOne(std::size_t index);

		void ReportSaveFailure(const std::filesystem::path& path, const Core::Error& error);

		void ReportFailure(Core::ToastKind kind, std::string title, std::string_view message);

		void RequestSave(std::size_t index);

		std::optional<std::size_t> FindActiveTextIndex();

		Core::OpenItem* FindActiveTextItem();

		std::optional<std::filesystem::path> FindActiveHexTab() const;

		void RunOnHexTab(const std::filesystem::path& path, EditorAction action, bool fromMenu);

		void QueueOnActiveDocument(EditorAction action, bool fromMenu);

		bool CanRunOnActiveDocument(EditorAction action);

		void RequestSaveOfActiveDocument();

		bool ActiveTabHoldsDocument();

		void RequestCloseOfActiveTab();

		void RequestSwitchSourceHeaderOfActiveDocument();

		bool CanSwitchSourceHeaderOfActiveDocument();

		void DrawFileTabItems(std::size_t index, const std::filesystem::path& workspaceRoot);

		unsigned int DockNodeForIdentity(const std::string& identity) const;

		void MovePendingDockTarget(const std::string& oldIdentity,
								   const std::string& newIdentity);

		void RenderSizeQuestion();

		void RenderReloadConflict(const ImVec4& danger);

		void RenderOverwriteConfirm(const ImVec4& danger);

		void RenderCloseConfirm(const ImVec4& danger);

		void AskGoToLine();

		// Brings the tab `step` places after the active one to the front, round the ends, in the order the tabs opened.
		void StepActiveTab(int step);

		void AskSaveAs();

		// Writes the document of `from` to `to`, opens `to` in its place and closes `from`'s tab, whose file is left as
		// it was on disk.
		void SaveDocumentAs(const std::filesystem::path& from, const std::filesystem::path& to);
		void RenderGoToLine();

		bool StillNeedsCloseConfirmation(const std::filesystem::path& path);


		void RenderDocumentWindows(const CommandContext& context, float fontSize,
								   Core::IndentStyle indentStyle, unsigned int fallbackDockNode,
								   bool wordWrap, ChangeMarkerDisplay changeDisplay,
								   bool highlightOccurrences, bool lineAuthor, HoverSettings hoverSettings,
								   const ResolvedScheme& scheme, ReferencesPanel& referencesPanel,
								   Core::TabCloseRequests& closeRequests,
								   const std::filesystem::path& workspaceRoot, EditorSettings& settings);

		// A picture or the bytes of a file, as a tab drew it.
		struct ShownView
		{
			std::vector<ViewStatusItem> Status;

			// Where a picture is laid out; empty for bytes.
			ImRect Area;
			bool Focused = false;

			// True when the tab drew bytes: a hex tab, or an image that cannot be shown.
			bool Bytes = false;
		};

		ShownView RenderImageTab(const CommandContext& context, const Core::OpenItem& item, EditorSettings& settings,
								 float fontSize, const ResolvedScheme& scheme, const std::filesystem::path& workspaceRoot,
								 bool updatedAsShown);

		ShownView RenderHexTab(const CommandContext& context, const Core::OpenItem& item, float fontSize,
							   const ResolvedScheme& scheme, const std::filesystem::path& workspaceRoot,
							   std::string_view notice = {});

		// Readies the bytes of every image that cannot be shown, before the frame's hex files are updated, and lets go of
		// those of an image that can.
		void PrepareImageFallbacks();

		bool DrewPicture(const std::filesystem::path& path) const;

		bool DrewBytes(const std::filesystem::path& path) const;

		std::optional<std::filesystem::path> ViewTakingTheZoom() const;

		void CheckViewOnDisk(std::size_t index);

		bool IsNewSinceLastFrame(const std::string& identity) const;

		unsigned int ChooseDockTarget(const std::string& identity,
									  unsigned int fallbackDockNode);

		void ActivateAndRememberNode(std::size_t index);

		void CloseAndForgetLayout(std::size_t index);

		void CloseIfOpen(const std::filesystem::path& path);

		void ForgetLayoutOfWindowsClosedElsewhere(
			const std::vector<std::string>& startingIdentities);

		static constexpr const char* kSizeQuestionTitle = "Large file##EditorSizeQuestion";
		static constexpr const char* kReloadConflictTitle =
			"File changed on disk##EditorReloadConflict";
		static constexpr const char* kOverwriteConfirmTitle =
			"Overwrite unsaved changes on disk?##EditorOverwriteConfirm";
		static constexpr const char* kCloseConfirmTitle = "Unsaved changes##EditorCloseConfirm";
		static constexpr const char* kGoToLineTitle = "Go to Line##EditorGoToLine";
		static constexpr const char* kDefinitionChoicesTitle = "##EditorDefinitionChoices";

		struct LargeFileQuestion
		{
			std::filesystem::path Path;
			bool Permanent = false;
			std::uint64_t Size = 0;

			bool operator==(const LargeFileQuestion&) const = default;
		};

		Core::DocumentSet& m_Documents;
		ActiveEditorTarget& m_ActiveEditor;
		ImageTabs& m_Images;
		HexFiles& m_Hex;

		Core::EventBus& m_Events;

		std::filesystem::path m_WorkspaceRoot;

		DocumentServerPort& m_Server;

		// WARNING: Declared right after m_Server, whose services fill it; the order is the
		// construction and destruction order.
		const DiagnosticsModel& m_Diagnostics;

		ChangeMarkersModel& m_ChangeMarkers;

		BlameModel& m_Blame;

		PreviewSemantics& m_PreviewSemantics;

		AnalysisCache& m_Analysis;

		AnalysisStore& m_AnalysisStore;

		WorkspaceAnalysis& m_WorkspaceAnalysis;

		ModalGate& m_Modals;

		unsigned int m_LastFocusedNode = 0;

		GitDecorationView m_GitDecorations;

		ModalQuestion<LargeFileQuestion> m_SizeQuestion;

		std::vector<std::string> m_PreviousIdentities;

		std::unordered_map<std::string, unsigned int> m_PendingDockTargets;

		bool m_WasFocused = true;

		// The Line author setting as of the last frame. With it off, a document's blame is kept only while its peek is
		// open, so no blame is held for a file nobody is looking at the authors of.
		bool m_LineAuthor = false;

		// The Line author delay setting as of the last frame.
		int m_LineAuthorDelayMs = 0;

		// This frame's Escape belongs to the navigation bubble on screen, so no document's peek or find bar closes on it.
		bool m_BubbleTakesEscape = false;

		ModalQuestion<std::filesystem::path> m_ReloadConflict;

		struct OverwriteQuestion
		{
			std::filesystem::path Path;

			// Asked by Save in the close question, which promised to close the tab once the file is written.
			bool ThenClose = false;

			bool IsSameQuestion(const OverwriteQuestion& other) const { return Path == other.Path; }
		};

		ModalQuestion<OverwriteQuestion> m_OverwriteConfirm;

		ModalQuestion<std::filesystem::path> m_CloseConfirm;

		// The document Go to Line was asked for, and what is typed in its field.
		ModalQuestion<std::filesystem::path> m_GoToLine;
		std::string m_GoToLineText;

		// The system's save dialog, which runs on a thread of its own and answers through Poll.
		Core::AsyncDialog m_SaveAsDialog;

		std::optional<std::filesystem::path> m_CloseRequested;

		struct TransientMessage
		{
			std::string Text;
			double ExpiresAt = 0.0;
		};

		std::optional<TransientMessage> m_TransientMessage;

		std::vector<Location> m_DefinitionChoices;

		CaretScreenPosition m_DefinitionChoicesAt;

		std::vector<CandidateLine> m_DefinitionChoiceLines;
		std::size_t m_DefinitionChoiceLinesTabWidth = 0;

		NavigationBubble m_Bubble;
		ImVec2 m_BubbleSize{};

		struct ReadWrap
		{
			std::uint64_t Ticket = 0;
			std::optional<WrapGeometry> Geometry;

			bool operator==(const ReadWrap&) const = default;
		};

		struct DocumentState
		{
			std::optional<LensState> Lens;

			ScrollAnchor Anchor;

			std::optional<PendingTopLine> PendingTop;

			std::optional<std::size_t> TopLineOnLoad;

			std::optional<WrapGeometry> PaneWrapWhileRead;

			std::optional<ReadWrap> WrapHandedToRead;

			CaretScreenPosition CaretScreen;

			CaretPlacement Caret;

			EditorWidgetState Widget;

			HoverState Hover;

			// ImGui::GetTime() of the last edit made through the editor; the lowest double when there was none.
			double LastEditAt = std::numeric_limits<double>::lowest();

			// The caret's line as the line author last saw it, and the ImGui::GetTime() it came to that line, for the
			// rest the author waits for.
			std::optional<std::size_t> AuthorCaretLine;
			double AuthorCaretLineSince = 0.0;

			// At most one peek a view; it survives switching to another tab and back.
			std::optional<LinePeekState> Peek;
		};

		std::map<std::filesystem::path, DocumentState> m_States;

		std::set<std::string> m_RestoredIdentities;

		std::set<std::filesystem::path> m_Shown;

		// The pictures and bytes drawn last frame, the one whose window had the keyboard focus, and whether any tab had
		// it. All are kept as they were for the whole of the next frame, so a command run while it draws sees what the
		// user saw.
		std::map<std::filesystem::path, ShownView> m_ShownViews;
		std::optional<std::filesystem::path> m_FocusedView;
		bool m_DocumentFocused = false;

		// The selection and scroll of every tab that has shown bytes, by the tab's path, until the tab closes.
		std::map<std::filesystem::path, HexViewState> m_HexViews;

		// WARNING: Constructed after m_Server, which it holds.
		LspAnswerRouter m_Router;

		// WARNING: Constructed after m_Router and m_Server, which it holds.
		DocumentStates m_DocumentStates{m_Server, m_Router, m_Diagnostics};

		FindBar m_FindBar;

		ExpectedWrites m_ExpectedWrites;

		std::optional<WrapGeometry> m_WrapOfLastDrawnEditor;
		std::optional<WrapGeometry> m_WrapHandedToLoads;
	};

	std::string SaveFailureMessage(const std::filesystem::path& path, std::string_view error);

	std::string DocumentWindowName(const Core::DocumentSet& documents, std::size_t index);
}
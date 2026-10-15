#pragma once

#include "UI/Dialogs/ModalGate.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/EditorWidgetState.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Navigation/CandidateLine.h"
#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/ExpectedWrites.h"
#include "Editor/Overlays/LensModel.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Editor/ScrollAnchor.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Editor/TextEditorWidget.h"

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
#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
	struct EditorFrame
	{
		float FontSize;
		Core::IndentStyle Indent;
		unsigned int FallbackDockNode;
		bool WordWrap;
		ChangeMarkerDisplay ChangeMarkers;
		HoverSettings Hover;
		const ResolvedScheme& Scheme;
		Core::TabCloseRequests& CloseRequests;
		const std::filesystem::path& WorkspaceRoot;
	};

	class DocumentWindows
	{
	public:
		DocumentWindows(Core::DocumentSet& documents, ModalGate& modals,
						DocumentServerPort& server, const DiagnosticsModel& diagnostics,
						ChangeMarkersModel& changeMarkers, PreviewSemantics& previewSemantics, AnalysisCache& analysis,
						AnalysisStore& analysisStore, WorkspaceAnalysis& workspaceAnalysis, Core::EventBus& events)
			: m_Documents(documents),
			  m_Events(events),
			  m_Server(server),
			  m_Diagnostics(diagnostics),
			  m_ChangeMarkers(changeMarkers),
			  m_PreviewSemantics(previewSemantics),
			  m_Analysis(analysis),
			  m_AnalysisStore(analysisStore),
			  m_WorkspaceAnalysis(workspaceAnalysis),
			  m_Modals(modals),
			  m_SizeQuestion(modals, kSizeQuestionTitle),
			  m_ReloadConflict(modals, kReloadConflictTitle),
			  m_OverwriteConfirm(modals, kOverwriteConfirmTitle),
			  m_CloseConfirm(modals, kCloseConfirmTitle),
			  m_Router(server, &DocumentWindows::FrameClock)
		{
		}

		void Render(const CommandContext& context, const EditorFrame& frame,
					ReferencesPanel& referencesPanel);

		void RequestOpen(const std::filesystem::path& path, bool permanent);

		void SetGitDecorations(const GitDecorationView& decorations) { m_GitDecorations = decorations; }

		WorkspaceState CaptureState() const;

		void CarryOverOpenDocuments();

		void RestoreState(const WorkspaceState& state);

		void HandleRootMoved(const std::filesystem::path& from, const std::filesystem::path& to);

		void HandleFilesChanged(const std::vector<std::filesystem::path>& directories,
								bool everything);

		void NotifyWindowFocus(bool focusedNow);

		bool SaveAllModified();

		bool HasModifiedDocuments() const;

		bool SaveDocuments(const std::vector<std::filesystem::path>& paths);

		void RevertDocuments(const std::vector<std::filesystem::path>& paths);

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		std::optional<std::string> GetStatusMessage() const;

		void SetTransientMessage(std::string text);

		void RequestDefinition(std::size_t index);

		void QueueEditorAction(std::size_t index, EditorAction action, bool fromMenu = false);

		void RequestSwitchSourceHeader(std::size_t index);

		void RequestReferences(std::size_t index, ReferencesPanel& panel);

		void OpenFind(Core::OpenItem& item) { m_FindBar.Open(item); }

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

			std::set<std::size_t> Asked;

			bool WasModified = false;

			bool NeedsRebuild = false;
		};

		struct DocumentState;

		DocumentState& StateFor(const std::filesystem::path& path);

		DocumentState* FindState(const std::filesystem::path& path);

		const DocumentState* FindState(const std::filesystem::path& path) const;

		LensState* FindLens(const std::filesystem::path& path);

		bool IsCarriedOver(const std::filesystem::path& path) const;

		SyntaxCache& SyntaxFor(const Core::OpenItem& item);

	public:
		bool IsAnalysing(const std::filesystem::path& path) const;

		bool IsShown(const std::filesystem::path& path) const;

	private:
		LensState* LensFor(const Core::OpenItem& item);
		SemanticOverlay& SemanticFor(const Core::OpenItem& item);

		DiagnosticOverlay& DiagnosticFor(const Core::OpenItem& item);

		void KeepAnalysis(const std::filesystem::path& path, const LensState& lens,
						  const SemanticOverlay& semantic);

		void RestoreAnalysis(const Core::OpenItem& item, DocumentState& state);

		static std::optional<std::filesystem::file_time_type> ModifiedTimeOf(
			const std::filesystem::path& path);

		void PruneDocumentStates();

		void UpdateLenses(const Core::OpenItem& item, LensState& state,
						  const EditorFrameResult& frame);

		void DropInFlightCounts(const std::filesystem::path& path, LensState& state);

		void AnnounceReload(const Core::OpenItem& item);

		void AskForVisibleCounts(const Core::OpenItem& item, LensState& state,
								 const EditorFrameResult& frame);

		void RequestReferencesAt(const Core::OpenItem& item, LanguageId language,
								 const FilePosition& where, const std::string& symbol,
								 ReferencesPanel& panel);

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

		bool SaveOne(std::size_t index);

		void ReportSaveFailure(const std::filesystem::path& path, const Core::Error& error);

		void ReportFailure(Core::ToastKind kind, std::string title, std::string_view message);

		void RequestSave(std::size_t index);

		std::optional<std::size_t> FindActiveTextIndex();

		Core::OpenItem* FindActiveTextItem();

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

		bool StillNeedsCloseConfirmation(const std::filesystem::path& path);


		void RenderDocumentWindows(const CommandContext& context, float fontSize,
								   Core::IndentStyle indentStyle, unsigned int fallbackDockNode,
								   bool wordWrap, ChangeMarkerDisplay changeDisplay,
								   HoverSettings hoverSettings,
								   const ResolvedScheme& scheme, ReferencesPanel& referencesPanel,
								   Core::TabCloseRequests& closeRequests,
								   const std::filesystem::path& workspaceRoot);

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
		static constexpr const char* kDefinitionChoicesTitle = "##EditorDefinitionChoices";

		struct LargeFileQuestion
		{
			std::filesystem::path Path;
			bool Permanent = false;
			std::uint64_t Size = 0;

			bool operator==(const LargeFileQuestion&) const = default;
		};

		Core::DocumentSet& m_Documents;

		Core::EventBus& m_Events;

		std::filesystem::path m_WorkspaceRoot;

		DocumentServerPort& m_Server;

		// WARNING: Declared right after m_Server, whose services fill it; the order is the
		// construction and destruction order.
		const DiagnosticsModel& m_Diagnostics;

		ChangeMarkersModel& m_ChangeMarkers;

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

		ModalQuestion<std::filesystem::path> m_ReloadConflict;

		ModalQuestion<std::filesystem::path> m_OverwriteConfirm;

		ModalQuestion<std::filesystem::path> m_CloseConfirm;

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

		struct DiagnosticGuard
		{
			std::size_t Revision = 0;
			std::size_t TabWidth = 0;
			std::uint64_t Generation = 0;
		};

		struct DocumentState
		{
			std::optional<SyntaxCache> Syntax;

			std::optional<LensState> Lens;

			SemanticOverlay Semantic;

			DiagnosticOverlay Diagnostics;
			DiagnosticGuard DiagnosticsBuiltFrom;

			ScrollAnchor Anchor;

			std::optional<PendingTopLine> PendingTop;

			bool CarriedOver = false;

			CaretScreenPosition CaretScreen;

			EditorWidgetState Widget;

			HoverState Hover;
		};

		std::map<std::filesystem::path, DocumentState> m_States;

		std::set<std::string> m_RestoredIdentities;

		std::set<std::filesystem::path> m_Shown;

		// WARNING: Constructed after m_Server, which it holds.
		LspAnswerRouter m_Router;

		FindBar m_FindBar;

		ExpectedWrites m_ExpectedWrites;
	};

	std::string SaveFailureMessage(const std::filesystem::path& path, std::string_view error);
}
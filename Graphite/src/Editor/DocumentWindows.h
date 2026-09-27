#pragma once

#include "Editor/ModalGate.h"
#include "Editor/DiagnosticOverlay.h"
#include "Editor/FindBar.h"
#include "Editor/AnalysisCache.h"
#include "Editor/ExpectedWrites.h"
#include "Editor/LensModel.h"
#include "Editor/PreviewSemantics.h"
#include "Editor/SemanticOverlay.h"
#include "Editor/ModalQuestion.h"
#include "Editor/TextEditorWidget.h"

#include "Git/ChangeMarkersModel.h"

#include "Lang/Analysis/AnalysisStore.h"
#include "Lang/Analysis/WorkspaceAnalysis.h"
#include "Lang/Diagnostics/DiagnosticsModel.h"
#include "Lang/Server/LanguageServices.h"
#include "Lang/Syntax/SyntaxCache.h"

#include "Panels/ReferencesPanel.h"
#include "Persistence/PersistedState.h"
#include "Search/PreviewContent.h"

#include "Core/Base/Timestep.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {

	class CommandRegistry;
	class Keymap;
}
namespace Core {

	class DocumentSet;

}

namespace Graphite {

	// The settings one frame is drawn under. Bundled into a struct with a named field per
	// setting, in the same spirit as StatusMessages in StatusBar.h:15 — as a flat parameter
	// list this is exactly the shape ADR 0034 calls out: a compound value taken apart into
	// individual elements, which a caller can pass in the wrong order with nothing to catch
	// it. Designated initialisation at the one call site makes that mistake impossible rather
	// than merely unlikely. Scheme is kept by reference rather than copied: ResolvedScheme
	// holds two colour arrays, and every EditorFrame is a temporary built at the call site and
	// passed straight into Render, so the reference never outlives what it points at.
	struct EditorFrame
	{
		float FontSize;
		Core::IndentStyle Indent;
		unsigned int FallbackDockNode;
		bool WordWrap;
		ChangeMarkerDisplay ChangeMarkers;
		const ResolvedScheme& Scheme;
		Core::TabCloseRequests& CloseRequests;
		const std::filesystem::path& WorkspaceRoot;
	};

	class DocumentWindows
	{
	public:
		DocumentWindows(Core::DocumentSet& documents, ModalGate& modals,
						LanguageServices& languageServices, const DiagnosticsModel& diagnostics,
						ChangeMarkersModel& changeMarkers, PreviewSemantics& previewSemantics, AnalysisCache& analysis,
						AnalysisStore& analysisStore, WorkspaceAnalysis& workspaceAnalysis)
			: m_Documents(documents),
			  m_LanguageServices(languageServices),
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
			  m_CloseConfirm(modals, kCloseConfirmTitle)
		{
		}

		void Render(const CommandContext& context, const EditorFrame& frame,
					ReferencesPanel& referencesPanel);

		void RequestOpen(const std::filesystem::path& path, bool permanent);

		// The open file documents as they stand, for the next launch. A tab holding no
		// document — its file went missing or would not load — is left out, and so is one
		// carried over from a folder this one replaced (see CarryOverOpenDocuments).
		WorkspaceState CaptureState() const;

		// Marks every open document as carried over from the folder about to be replaced.
		// Opening another folder over one leaves its documents open, but a folder's tabs are
		// written to that folder and never to another, so CaptureState leaves these out. A path
		// stops being carried over when its tab closes, when RequestOpen opens it again from the
		// new folder, or when the new folder's restored tabs name it too.
		void CarryOverOpenDocuments();

		// Reopens what CaptureState returned last time: tabs whose files are gone are skipped,
		// each caret and view goes back where it was, and the active tab is brought forward.
		void RestoreState(const WorkspaceState& state);

		void HandleRootMoved(const std::filesystem::path& from, const std::filesystem::path& to);

		void HandleFilesChanged(const std::vector<std::filesystem::path>& directories,
								bool everything);

		void NotifyWindowFocus(bool focusedNow);

		bool SaveAllModified();

		bool HasModifiedDocuments() const;

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		std::optional<std::string> GetStatusMessage() const;

		// Asks the language server serving the document at `index` for the definition of
		// whatever the caret sits on, and routes the reply through ResolveDefinition once it
		// arrives. The active document's index, not a path: F12 and Ctrl+click both already know
		// which tab they mean, and resolving that from a path would need a second lookup that
		// could disagree with the one the caller already did.
		void RequestDefinition(std::size_t index);

		// Asks the language server serving the document at `index` for every reference to
		// whatever word the caret sits on or touches, and shows the reply in `panel`. The
		// active document's index, for the same reason RequestDefinition takes one: F12's sibling
		// Shift+F12 knows which tab it means without a second lookup that could disagree.
		void RequestReferences(std::size_t index, ReferencesPanel& panel);

		// Opens a document's find bar. Routed through here rather than called on the bar directly,
		// because the bar belongs to whatever draws the documents and the command that binds
		// Ctrl+F to it has no business knowing where that is.
		void OpenFind(Core::OpenItem& item) { m_FindBar.Open(item); }

		// Says that a change about to arrive for these paths was made by this application,
		// so the file should be reloaded rather than asked about. Called after the write has
		// succeeded, never before it is sent. See ExpectedWrites for what bounds it.
		void ExpectExternalWrite(const std::vector<std::filesystem::path>& paths)
		{
			m_ExpectedWrites.Expect(paths);
		}

		// Ages the expectations, so one whose event never arrives cannot silence a genuine
		// external change later.
		void TickExpectedWrites(Core::Timestep ts) { m_ExpectedWrites.Tick(ts.GetSeconds()); }

		// Forgets every outstanding expectation. Called when the folder closes: an
		// expectation registered against one repository has no business claiming a path in
		// whatever folder opens next.
		void ClearExpectedWrites() { m_ExpectedWrites.Clear(); }

	private:
		// Everything the reference-count rows need for one document. LensModel alone is not
		// enough: the declarations behind it have to be kept so the anchors can be rebuilt when
		// the wrap width changes — a lens anchors to a ROW, and a rewrap moves every row without
		// any declaration having moved.
		struct LensState
		{
			LensModel Model;

			std::vector<Declaration> Declarations;

			// The wrap width the anchors were last built at, so a rewrap is noticed. Only
			// meaningful once a reply has arrived.
			std::size_t BuiltAt = 0;

			bool HaveDeclarations = false;

			// Document rows an AskReferences is already outstanding for, so a row visible for a
			// second is asked about once rather than sixty times. Cleared whenever the anchors
			// move, because the rows it holds no longer mean what they did.
			std::set<std::size_t> Asked;

			// Symbols are wanted from the moment the document opens and again after every edit.
			// One request may be outstanding at a time, and a failed one — a server still
			// starting, or still indexing — is retried on a clock rather than on the next frame,
			// which would be one request per frame for as long as the server took.
			bool WantSymbols = true;
			bool SymbolsOutstanding = false;
			double NextSymbolsAt = 0.0;

			// What the serving server's state was the last frame this document drew, so a restart
			// is noticed. A row already in `Asked` is never asked about again while it stays
			// there, and the client that died failed everything it held outstanding — so without
			// this the rows on screen at the moment of a crash keep a blank count for the rest of
			// the session, however healthy the replacement server is.
			ServerState LastServerState = ServerState::NotConfigured;

			// Whether the document had unsaved edits the last frame it drew. Read at close, by
			// which point the document is gone from DocumentSet and cannot be asked - and its
			// answer decides whether this analysis is worth keeping at all.
			bool WasModified = false;

			// Set when declarations arrive from somewhere that cannot anchor them itself, and
			// cleared by the first rebuild that can. See RestoreAnalysis for the one case.
			bool NeedsRebuild = false;
		};

		SyntaxCache& SyntaxFor(const Core::OpenItem& item);

		void PruneSyntaxCaches();

	public:
		// Whether what this document looks like is still being settled - its declarations and its
		// colours, the two answers that arrive once per open and change the whole file at once.
		//
		// The status bar's subject, and the reason it has one: a server can be perfectly ready
		// while the file on screen is not, and without this the bar answers about the server when
		// the question is about the file.
		//
		// Reference counts are deliberately not part of this. They are asked for a screen at a
		// time as rows scroll into view, so a file being read steadily always has some in flight,
		// and counting them would leave the bar alternating between two words for as long as
		// anybody kept scrolling.
		bool IsAnalysing(const std::filesystem::path& path) const;

	private:

		// Null when no language server is configured for this document's language, which is what
		// puts a null LensModel on EditorView and spares the widget a branch on whether a server
		// exists. Opens the document with the server the first time it is asked for.
		LensState* LensFor(const Core::OpenItem& item);
		SemanticOverlay& SemanticFor(const Core::OpenItem& item);

		// The drawable form of what m_Diagnostics holds for this document, rebuilt only when one
		// of the three inputs to the conversion has moved since the last call: the buffer's
		// revision, the tab width, or whether m_Diagnostics has answered anew for this path. Kept
		// per document rather than recomputed every frame because the conversion walks every
		// line a diagnostic touches, and most frames change none of the three. Always returns a
		// valid overlay, empty for a document nothing has been said about - the same idiom
		// SemanticFor above uses.
		DiagnosticOverlay& DiagnosticFor(const Core::OpenItem& item);

		// Analysis outlives its tab: KeepAnalysis puts a closing document's answers away and
		// RestoreAnalysis hands them to a reopening one. See AnalysisCache for why what comes back
		// is a first frame rather than an answer - the server is asked either way.
		void KeepAnalysis(const std::filesystem::path& path, const LensState& state);

		// Drops the tickets outstanding for a document that has gone, so nothing waits on answers
		// nobody will read.
		void ForgetPendingFor(const std::filesystem::path& path);

		void RestoreAnalysis(const Core::OpenItem& item, LensState& state);

		static std::optional<std::filesystem::file_time_type> ModifiedTimeOf(
			const std::filesystem::path& path);



		void PruneLensStates();

		// Sends what the frame's edits and geometry mean for this document's lenses, and asks for
		// the counts the rows on screen still need.
		void UpdateLenses(const Core::OpenItem& item, LensState& state,
						  const EditorFrameResult& frame);

		// Forgets every reference count this document has in flight, and lets the rows they were
		// asked about be asked about again. Shared by the two things that move the text under the
		// anchors: an edit made in the widget, and a reload that replaced the buffer outright.
		void DropInFlightCounts(const std::filesystem::path& path, LensState& state);

		// Tells the server about a buffer replaced from disk and forgets what the old text was
		// answered about. Nothing else does: `didChange` otherwise goes out only for a frame the
		// editor itself edited, so a reload would leave the server holding text the user is no
		// longer looking at, with no symptom anywhere to say so.
		void AnnounceReload(const Core::OpenItem& item);

		void AskForSymbols(const Core::OpenItem& item, LensState& state);

		void AskForVisibleCounts(const Core::OpenItem& item, LensState& state,
								 const EditorFrameResult& frame);

		// The shared tail of both entry points: cancels whatever references request was already
		// outstanding — a second question supersedes the first, on the same reasoning as
		// m_PendingDefinition — issues the new one, and shows `panel` the symbol it is now
		// working on.
		void RequestReferencesAt(const Core::OpenItem& item, LanguageId language,
								 const FilePosition& where, const std::string& symbol,
								 ReferencesPanel& panel);

		// The lens-click entry point: finds the declaration a clicked lens sits above — LensClicked
		// names the document row, not the declaration itself, since a lens row holds no text of
		// its own to identify it by — and asks about that declaration's position and name.
		void RequestReferencesForLens(const Core::OpenItem& item, const LensState& state,
									  std::size_t documentRow, ReferencesPanel& panel);

		// Routes everything the servers have answered since the last frame to the document that
		// asked. LanguageServices::TakeAnswers is the single drain point, so this is the only
		// place answers are collected.
		void CollectServerAnswers(ReferencesPanel& referencesPanel);

		void ApplySymbolAnswer(const std::filesystem::path& path,
							   const LanguageServices::Answer& answer);

		// Where DrawEditor last put the caret on screen for a document, kept so a request that
		// began at the keyboard — F12 never touches the mouse the way Ctrl+click does — still has
		// somewhere on screen to open a popup near. One frame stale at worst: the value from the
		// document's own last draw, read back before that document draws again.
		struct CaretScreenPosition
		{
			float X = 0.0f;
			float Y = 0.0f;
		};

		// The caret's offset at the moment RequestDefinition asked stands in for a generation:
		// nothing outside an edit moves it on its own, so an answer that finds a different offset
		// there now is about a question the caret has since moved on from, and jumping on its
		// strength would read as the editor jumping unprompted.
		struct PendingDefinition
		{
			LanguageServices::Ticket Ticket{};
			std::filesystem::path Path;
			std::size_t CaretOffset = 0;
			FilePosition From;
			CaretScreenPosition Screen;
		};

		void ApplyDefinitionAnswer(const PendingDefinition& pending,
								   const LanguageServices::Answer& answer);

		// Drawn like the other questions in Render(), before the per-document loop: OpenPopup is
		// called from ApplyDefinitionAnswer, itself reached from CollectServerAnswers, which also
		// runs before that loop, so this never has to run from inside a Begin/BeginChild pair.
		void RenderDefinitionChoices(const ResolvedScheme& scheme, std::size_t tabWidth);

		void SyncEveryDocument();

		void RespondToSync(std::size_t index, Core::FileSyncOutcome outcome);

		void ReloadOne(std::size_t index);

		void AskAboutSize(const std::filesystem::path& path, bool permanent);

		void LoadConfirmed(const std::filesystem::path& path, bool permanent);

		bool SaveOne(std::size_t index);

		void RequestSave(std::size_t index);

		void SetTransientMessage(std::string text);

		unsigned int DockNodeForIdentity(const std::string& identity) const;

		void MovePendingDockTarget(const std::string& oldIdentity,
								   const std::string& newIdentity);

		void RenderSizeQuestion();

		void RenderReloadConflict();

		void RenderOverwriteConfirm();

		void RenderCloseConfirm();

		bool StillNeedsCloseConfirmation(const std::filesystem::path& path);

		void RenderDocumentWindows(const CommandContext& context, float fontSize,
								   Core::IndentStyle indentStyle, unsigned int fallbackDockNode,
								   bool wordWrap, ChangeMarkerDisplay changeDisplay,
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
			std::filesystem::path path;
			bool permanent = false;
			std::uint64_t size = 0;

			bool operator==(const LargeFileQuestion&) const = default;
		};

		Core::DocumentSet& m_Documents;

		LanguageServices& m_LanguageServices;

		// Declared right after m_LanguageServices, which fills it - see GraphiteLayer.h for why
		// that order matters. Held by reference: GraphiteLayer owns it and feeds it once a frame,
		// before this class ever reads it.
		const DiagnosticsModel& m_Diagnostics;

		// The git change markers for every open document. Held by reference like m_Diagnostics
		// above: GraphiteLayer owns it and drives it once a frame. Not const, because drawing a
		// document is what tells the model its buffer changed.
		ChangeMarkersModel& m_ChangeMarkers;

		// The preview panes' own mediator to the language servers, held by reference like
		// m_LanguageServices above and declared right after it for the same reason: this class
		// does not own it, GraphiteLayer does, and it must outlive every document window.
		PreviewSemantics& m_PreviewSemantics;

		// Not owned: the background pass writes into the same cache, and one cache with two
		// producers is the whole point — an entry put there by the walk is the entry this class
		// restores when a tab opens on that file.
		AnalysisCache& m_Analysis;

		// The tier under it, not owned either, and with the same two producers: the background pass
		// writes what it analysed and this class writes what a closing tab knew. Asked after
		// m_Analysis when a tab opens, because a memory hit is both fresher and free.
		AnalysisStore& m_AnalysisStore;

		// Offered every answer this class does not recognise, the same way PreviewSemantics is and
		// for the same reason: CollectServerAnswers is the single drain point, so anything that
		// asks a question of its own has to be heard from here.
		WorkspaceAnalysis& m_WorkspaceAnalysis;

		// Held as well as handed to the questions below: the definition picker is an ordinary
		// popup rather than a ModalQuestion, and it still has to take the same gate — ImGui
		// cannot stack two popups at level 0, and opening this one over a question would destroy
		// that question rather than queue behind it.
		ModalGate& m_Modals;

		unsigned int m_LastFocusedNode = 0;

		ModalQuestion<LargeFileQuestion> m_SizeQuestion;

		std::vector<std::string> m_PreviousIdentities;

		std::unordered_map<std::string, unsigned int> m_PendingDockTargets;

		bool m_WasFocused = true;

		ModalQuestion<std::filesystem::path> m_ReloadConflict;

		ModalQuestion<std::filesystem::path> m_OverwriteConfirm;

		ModalQuestion<std::filesystem::path> m_CloseConfirm;

		struct TransientMessage
		{
			std::string text;
			double expiresAt = 0.0;
		};

		std::optional<TransientMessage> m_TransientMessage;

		// Keyed and pruned alongside m_Syntax, for the document it belongs to: RequestDefinition
		// reads the entry for whichever document it was asked about, which for F12 is the last
		// position that document's own draw left behind.
		std::map<std::filesystem::path, CaretScreenPosition> m_CaretScreen;

		// At most one definition request outstanding at a time: a second F12 (or Ctrl+click)
		// before the first has answered cancels it, on the same reasoning as m_PendingCounts —
		// only the most recent question is one the user is still waiting on.
		std::optional<PendingDefinition> m_PendingDefinition;

		// What ResolveDefinition left to choose from, drawn by RenderDefinitionChoices until a
		// choice is made or the popup is dismissed.
		std::vector<Location> m_DefinitionChoices;

		// Where the picker opens, kept from the answer that filled m_DefinitionChoices until
		// RenderDefinitionChoices can spend it. It cannot be spent where it is decided:
		// SetNextWindowPos writes into ImGui's next-window state, and every BeginPopupModal that
		// runs before the picker's own Begin clears that state on its early-out — the four
		// questions drawn ahead of it would each eat the position, and the list would open at
		// the mouse pointer, which is the one place F12 must never put it.
		CaretScreenPosition m_DefinitionChoicesAt;

		// Its own PreviewContent rather than borrowing SyntaxFor's caches: the choices can name
		// several different files, none of which need be open, and the popup outlives none of
		// them — a fresh Show per row is what a search preview already does for the same reason.
		PreviewContent m_DefinitionPreview;

		// What a references request currently outstanding was asked about. At most one at a
		// time, exactly like m_PendingDefinition: a second Shift+F12 or lens click before the
		// first has answered cancels it, because only the most recent question is one the user
		// is still waiting on, and its answer is dropped by generation — this ticket is the
		// generation.
		struct PendingReferences
		{
			LanguageServices::Ticket Ticket{};
			std::string Symbol;
		};

		std::optional<PendingReferences> m_PendingReferences;

		// Keyed by path rather than by index: the document list is reordered by dragging tabs
		// and compacted on close, and either would hand a document another one's colours.
		//
		// A side-map on a container that owns the identity: Core::DocumentSet::RewritePaths can
		// change item.Path (a rename on disk, or of a directory an open document lives under)
		// without telling this map, so the entry under the old path would otherwise sit here
		// for the rest of the process. PruneSyntaxCaches is what recovers that rather than a
		// rewrite at the rename site — the rename happens in Core, this map is Graphite's own,
		// and a prune is the one fix that does not need the rename to know this map exists.
		std::map<std::filesystem::path, SyntaxCache> m_Syntax;

		// Keyed and pruned exactly as m_Syntax is, and for the same reason: DocumentSet owns the
		// path this is keyed on and can rewrite it without telling this map, so an entry orphaned
		// by a rename would otherwise hold its declarations for the rest of the process.
		std::map<std::filesystem::path, LensState> m_Lenses;

		// What an outstanding ticket was asked for. Tickets are unique across every server, which
		// is what lets one map per question kind route an answer back to the document — and to the
		// row — that wanted it, without either server's numbering colliding with the other's.
		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSymbols;

		// The colours a server said this document's identifiers should take, one overlay per
		// document, beside m_Syntax and m_Lenses and pruned with them. Separate from the lens
		// state rather than folded into it: they answer different questions about the same file
		// and nothing about one is needed to read the other.
		std::map<std::filesystem::path, SemanticOverlay> m_Semantic;

		// Where each document's viewport sat when it last drew, so a screen-row mapping that
		// changes between frames — a lens row arriving, a narrower window rewrapping — does not
		// slide the file out from under the reader. Beside m_Semantic and pruned with it.
		//
		// Kept for every document rather than only for served ones: lenses are the trigger that
		// was noticed, but rewrapping moves rows in a plain text file just as well, and an anchor
		// costs three numbers.
		std::map<std::filesystem::path, ScrollAnchor> m_Anchors;

		// Top lines restored from the last launch that no frame has shown yet. See PendingTopLine.
		std::map<std::filesystem::path, PendingTopLine> m_PendingTopLines;

		// Documents still open from a folder another one replaced. See CarryOverOpenDocuments.
		// Swept with m_Anchors when their tabs close.
		std::set<std::filesystem::path> m_CarriedOver;

		// Identities of documents reopened by RestoreState whose window has not been submitted
		// yet. Their first frame docks them where imgui.ini remembers rather than where the
		// user last focused.
		std::set<std::string> m_RestoredIdentities;

		// What DiagnosticFor last built the overlay from - the buffer revision, the tab width,
		// and the generation of the answer m_Diagnostics held for this path, so a rebuild is
		// skipped on every frame that changed none of them. Generation rather than the answered
		// version: two publications can carry the same version and different content (a server
		// that omits version, or clangd republishing once its preamble build finishes), and a
		// guard keyed on the version would call those identical and never rebuild. Generation is
		// bumped by DiagnosticsModel::Apply on every publication, so it never repeats.
		struct DiagnosticGuard
		{
			std::size_t Revision = 0;
			std::size_t TabWidth = 0;
			std::uint64_t Generation = 0;
		};

		// The drawable form of what m_Diagnostics holds for one document, beside m_Semantic and
		// pruned with it for the same reason: DiagnosticFor is called for every drawn document,
		// including those no server serves.
		std::map<std::filesystem::path, DiagnosticOverlay> m_DiagnosticOverlays;
		std::map<std::filesystem::path, DiagnosticGuard> m_DiagnosticGuards;

		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSemantic;

		struct PendingCount
		{
			std::filesystem::path Path;
			std::size_t DocumentRow = 0;
		};

		std::map<LanguageServices::Ticket, PendingCount> m_PendingCounts;

		// One for every document rather than one each: what it holds is which document wants the
		// keyboard next, and only one can.
		FindBar m_FindBar;

		// Paths a write this application made is about to touch, so the reload conflict does
		// not ask about a change the user already confirmed once. See ExpectedWrites.
		ExpectedWrites m_ExpectedWrites;
	};
}
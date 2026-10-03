#pragma once

#include "UI/ModalGate.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/EditorWidgetState.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/ExpectedWrites.h"
#include "Editor/Overlays/LensModel.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Editor/ScrollAnchor.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "UI/ModalQuestion.h"
#include "Editor/TextEditorWidget.h"

#include "Git/Changes/ChangeMarkersModel.h"

#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Syntax/SyntaxCache.h"

#include "Search/ReferencesPanel.h"
#include "Persistence/PersistedState.h"
#include "Search/PreviewContent.h"

#include "Core/Base/Timestep.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <cstddef>
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
						AnalysisStore& analysisStore, WorkspaceAnalysis& workspaceAnalysis)
			: m_Documents(documents),
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

		// Saves the open documents at these paths, stopping at the first that cannot be written.
		// A path with no open document is skipped: there is nothing of it to save.
		bool SaveDocuments(const std::vector<std::filesystem::path>& paths);

		// Reloads the open documents at these paths from disk, discarding their unsaved edits
		// without asking: whoever calls this has already asked.
		void RevertDocuments(const std::vector<std::filesystem::path>& paths);

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		std::optional<std::string> GetStatusMessage() const;

		// Shows `text` in the status bar for a few seconds, replacing whatever it showed. Public
		// for GraphiteLayer, which forwards StatusMessageEvent here so other panels can speak
		// through the bar without knowing the editor.
		void SetTransientMessage(std::string text);

		// Asks the language server serving the document at `index` for the definition of
		// whatever the caret sits on, and routes the reply through ResolveDefinition once it
		// arrives. The active document's index, not a path: F12 and Ctrl+click both already know
		// which tab they mean, and resolving that from a path would need a second lookup that
		// could disagree with the one the caller already did.
		void RequestDefinition(std::size_t index);

		// Hands an editor action to the document at `index`, to be applied when it next draws.
		void QueueEditorAction(std::size_t index, EditorAction action);

		// Asks the language server for the header or source belonging to the document at
		// `index`, and opens it when the answer arrives.
		void RequestSwitchSourceHeader(std::size_t index);

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
		void TickExpectedWrites(Core::Timestep ts) { m_ExpectedWrites.Tick(ts); }

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

			// Whether the document had unsaved edits the last frame it drew. Read at close, by
			// which point the document is gone from DocumentSet and cannot be asked - and its
			// answer decides whether this analysis is worth keeping at all.
			bool WasModified = false;

			// Set when declarations arrive from somewhere that cannot anchor them itself, and
			// cleared by the first rebuild that can. See RestoreAnalysis for the one case.
			bool NeedsRebuild = false;
		};

		// Everything this class keeps for one open document. Defined beside m_States below.
		struct DocumentState;

		// The state kept for `path`, created empty the first time it is asked for.
		DocumentState& StateFor(const std::filesystem::path& path);

		// The lens state of the document at `path`, or null when it has none: no tab, or no
		// language server serving it.
		LensState* FindLens(const std::filesystem::path& path);

		bool IsCarriedOver(const std::filesystem::path& path) const;

		SyntaxCache& SyntaxFor(const Core::OpenItem& item);

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

		// Whether this document's window drew its contents on the last frame: false while another
		// tab of its node is in front of it, or its floating window is collapsed. Last frame's,
		// because the status bar asks before this frame's documents have drawn.
		bool IsShown(const std::filesystem::path& path) const;

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
		void KeepAnalysis(const std::filesystem::path& path, const LensState& lens,
						  const SemanticOverlay& semantic);

		void RestoreAnalysis(const Core::OpenItem& item, DocumentState& state);

		static std::optional<std::filesystem::file_time_type> ModifiedTimeOf(
			const std::filesystem::path& path);

		// Drops the state of every document that is no longer open, telling the server about the
		// ones it served and putting their analysis away first. See m_States for why this runs
		// once a frame rather than where a path changes.
		void PruneDocumentStates();

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

		void AskForVisibleCounts(const Core::OpenItem& item, LensState& state,
								 const EditorFrameResult& frame);

		// The shared tail of both entry points: asks through m_Router, which cancels whatever
		// references request was already outstanding - a second question supersedes the first -
		// and shows `panel` the symbol it is now working on.
		void RequestReferencesAt(const Core::OpenItem& item, LanguageId language,
								 const FilePosition& where, const std::string& symbol,
								 ReferencesPanel& panel);

		// The lens-click entry point: finds the declaration a clicked lens sits above — LensClicked
		// names the document row, not the declaration itself, since a lens row holds no text of
		// its own to identify it by — and asks about that declaration's position and name.
		void RequestReferencesForLens(const Core::OpenItem& item, const LensState& state,
									  std::size_t documentRow, ReferencesPanel& panel);

		// Routes everything the servers have answered since the last frame to the document that
		// asked, through m_Router. DocumentServerPort::TakeAnswers is the single drain point, so
		// this is the only place answers are collected.
		void CollectServerAnswers(ReferencesPanel& referencesPanel);

		// What m_Router hands on. ApplySymbolAnswer says whether the document took them: false
		// for one with no lens or no buffer to anchor them to.
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

		// The clock m_Router runs its retry delay on: ImGui's, in seconds.
		static double FrameClock();

		// Drawn like the other questions in Render(), before the per-document loop: OpenPopup is
		// called from ApplyDefinitionAnswer, itself reached from CollectServerAnswers, which also
		// runs before that loop, so this never has to run from inside a Begin/BeginChild pair.
		void RenderDefinitionChoices(const ResolvedScheme& scheme, std::size_t tabWidth);

		void SyncEveryDocument();

		// What one pass over the open documents reloaded: the writes the application expected -
		// see ExpectExternalWrite - which are announced once for the whole pass, and whether any
		// reload failed, whose own message then stands.
		struct ReloadBatch
		{
			std::vector<std::size_t> Expected;
			bool AnyFailed = false;
		};

		void RespondToSync(std::size_t index, Core::FileSyncOutcome outcome, ReloadBatch& batch);

		void ReloadExpected(std::size_t index, ReloadBatch& batch);

		void AnnounceExpectedReloads(const ReloadBatch& batch);

		// `announce` false leaves the success message to the caller, for a reload whose cause is
		// not "changed on disk". A failure is always said.
		bool ReloadOne(std::size_t index, bool announce = true);

		// The open document at `path`, matched the way Windows compares paths rather than
		// byte for byte.
		std::optional<std::size_t> FindOpen(const std::filesystem::path& path) const;

		void AskAboutSize(const std::filesystem::path& path, bool permanent);

		void LoadConfirmed(const std::filesystem::path& path, bool permanent);

		bool SaveOne(std::size_t index);

		void RequestSave(std::size_t index);

		// What a file tab's context menu offers above the close actions every tab has.
		void DrawFileTabItems(std::size_t index, const std::filesystem::path& workspaceRoot);

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

		DocumentServerPort& m_Server;

		// Declared right after m_Server, whose services fill it - see GraphiteLayer.h for why
		// that order matters. Held by reference: GraphiteLayer owns it and feeds it once a frame,
		// before this class ever reads it.
		const DiagnosticsModel& m_Diagnostics;

		// The git change markers for every open document. Held by reference like m_Diagnostics
		// above: GraphiteLayer owns it and drives it once a frame. Not const, because drawing a
		// document is what tells the model its buffer changed.
		ChangeMarkersModel& m_ChangeMarkers;

		// The preview panes' own mediator to the language servers, held by reference like
		// m_Server above and declared right after it for the same reason: this class
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
			std::string Text;
			double ExpiresAt = 0.0;
		};

		std::optional<TransientMessage> m_TransientMessage;

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

		struct DocumentState
		{
			// Empty until the document first draws, which is when SyntaxFor gives it a lexer for
			// the language its path implies.
			std::optional<SyntaxCache> Syntax;

			// Empty for a document no language server serves, which is what puts a null
			// LensModel on EditorView. Filled by LensFor, which is also what opens the document
			// with the server, so a value here is a document the server has been told about and
			// PruneDocumentStates has to tell it closed.
			std::optional<LensState> Lens;

			// The colours a server said this document's identifiers should take. Separate from
			// the lens state rather than folded into it: they answer different questions about
			// the same file, nothing about one is needed to read the other, and a document no
			// server serves still draws through an empty overlay.
			SemanticOverlay Semantic;

			// The drawable form of what m_Diagnostics holds for this document, and what it was
			// last built from. See DiagnosticFor.
			DiagnosticOverlay Diagnostics;
			DiagnosticGuard DiagnosticsBuiltFrom;

			// Where the viewport sat when the document last drew, so a screen-row mapping that
			// changes between frames — a lens row arriving, a narrower window rewrapping — does
			// not slide the file out from under the reader. Kept for every document rather than
			// only for served ones: rewrapping moves rows in a plain text file just as well.
			ScrollAnchor Anchor;

			// A top line restored from the last launch that no frame has shown yet. See
			// PendingTopLine.
			std::optional<PendingTopLine> PendingTop;

			// Still open from a folder another one replaced. See CarryOverOpenDocuments.
			bool CarriedOver = false;

			// Where the caret last drew. RequestDefinition reads it for whichever document it
			// was asked about, which for F12 is the last position that document's own draw left
			// behind.
			CaretScreenPosition CaretScreen;

			// The caret's blink, a held undo's ramp and the last bracket search.
			EditorWidgetState Widget;

			// The hover's tracker, the answers held for it and last frame's tooltip. Handed to
			// the widget only for a document a language server serves; ApplyHoverAnswer and
			// ApplyPreviewAnswer fill in the answers.
			HoverState Hover;
		};

		// One entry per open document, keyed by path rather than by index: the document list is
		// reordered by dragging tabs and compacted on close, and either would hand a document
		// another one's state.
		//
		// A side-map on a container that owns the identity: Core::DocumentSet::RewritePaths can
		// change item.Path (a rename on disk, or of a directory an open document lives under)
		// without telling this map, so the entry under the old path would otherwise sit here for
		// the rest of the process. PruneDocumentStates is what recovers that rather than a
		// rewrite at the rename site — the rename happens in Core, this map is Graphite's own,
		// and a prune is the one fix that does not need the rename to know this map exists. The
		// renamed document starts over under its new path: a fresh syntax cache lexed for
		// whatever language the new extension implies, and a fresh didOpen with the server.
		// Only CarriedOver follows the rename, moved by HandleRootMoved, which is the one rename
		// this class hears about.
		std::map<std::filesystem::path, DocumentState> m_States;

		// Identities of documents reopened by RestoreState whose window has not been submitted
		// yet. Their first frame docks them where imgui.ini remembers rather than where the
		// user last focused.
		std::set<std::string> m_RestoredIdentities;

		// The documents whose window was visible on the last frame, rebuilt by every
		// RenderDocumentWindows; see IsShown.
		std::set<std::filesystem::path> m_Shown;

		// Every question outstanding with the language servers - definition, references,
		// declarations, colours, counts - and where its answer goes. Constructed after m_Server,
		// which it holds.
		LspAnswerRouter m_Router;

		// One for every document rather than one each: what it holds is which document wants the
		// keyboard next, and only one can.
		FindBar m_FindBar;

		// Paths a write this application made is about to touch, so the reload conflict does
		// not ask about a change the user already confirmed once. See ExpectedWrites.
		ExpectedWrites m_ExpectedWrites;
	};
}
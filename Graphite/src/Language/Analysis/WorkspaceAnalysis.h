#pragma once

#include "Language/Analysis/FileTextSource.h"
#include "Language/Api/LanguageId.h"
#include "Language/Server/LanguageServices.h"

#include "Editor/Overlays/AnalysisCache.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <deque>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class AnalysisStore;

	enum class ServerReadiness : std::uint8_t
	{
		Ready,
		ComingUp,
		CannotRun
	};

	ServerReadiness ReadinessFrom(const ServerStatus& status);

	// What WorkspaceAnalysis needs from a language server and nothing more.
	//
	// Separate from LanguageServices for the reason PreviewServerPort is: the bookkeeping above
	// it — a bounded number of files at once, never two analyses of one path, yield to the open
	// document, cache what came back — is worth asserting without a clangd process.
	class WorkspaceAnalysisPort
	{
	public:
		virtual ~WorkspaceAnalysisPort() = default;

		WorkspaceAnalysisPort() = default;
		WorkspaceAnalysisPort(const WorkspaceAnalysisPort&) = delete;
		WorkspaceAnalysisPort& operator=(const WorkspaceAnalysisPort&) = delete;

		virtual void Open(const std::filesystem::path& path, LanguageId language,
						  std::string text) = 0;

		virtual LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
														   LanguageId language) = 0;

		virtual LanguageServices::Ticket AskSymbols(const std::filesystem::path& path,
													LanguageId language) = 0;

		virtual void Close(const std::filesystem::path& path, LanguageId language) = 0;

		virtual ServerReadiness ReadinessOf(const std::filesystem::path& path, LanguageId language) = 0;
		virtual bool HasChosenServer(LanguageId language) = 0;
	};

	// How far the pass has got, for the status bar. Total is what was queued, not what the tree
	// holds: until the walk finishes there is no such number, which is what Walking says.
	struct AnalysisProgress
	{
		std::size_t Done = 0;
		std::size_t Total = 0;
		bool Walking = false;
	};

	// Asks the server about every file in the workspace, a few at a time, so that opening one
	// paints in the frame it opens rather than after a round trip.
	//
	// The whole design is one sentence: never more than a small, bounded number of files open with
	// the server at once, and never at the expense of the file someone is looking at. The first is
	// what keeps the server's footprint independent of the size of the tree — a didOpen pins a
	// preamble and an AST, and a queue that opened a hundred files would cost a hundred of them,
	// so the limit is derived from the machine rather than from the queue. The second is what
	// keeps the feature from being felt: a background file's answers are worth nothing if the
	// active document's are behind them.
	//
	// The bound above one carries an invariant that used to come free. See IsOutstanding: while
	// one file was analysed at a time, two analyses of one path were impossible by construction,
	// and now they are impossible only because they are checked for.
	//
	// Results go into AnalysisCache, which DocumentWindows already reads when a document opens,
	// and into AnalysisStore, which is where they survive the session. Nothing here draws, and
	// nothing here is a source of truth: what it produces is a first frame, overwritten by the
	// server's own answer as soon as a tab asks its own question.
	//
	// Nothing here reads a file or waits for anything either. Text arrives through a
	// FileTextSource, which does the reading on a worker; the store encodes and writes on one of
	// its own; every method below runs on the UI thread and returns inside a frame.
	class WorkspaceAnalysis
	{
	public:
		// The store is nullable: a session with no folder open has none to consult, and the tests
		// that are about the pass rather than about the disk pass nothing.
		WorkspaceAnalysis(WorkspaceAnalysisPort& port, FileTextSource& texts, AnalysisCache& cache,
						  AnalysisStore* store);

		// Closes whatever it had open. A pending analysis outliving this object would leave a
		// document open with a server for the rest of the session.
		~WorkspaceAnalysis();

		WorkspaceAnalysis(const WorkspaceAnalysis&) = delete;
		WorkspaceAnalysis& operator=(const WorkspaceAnalysis&) = delete;

		// False drops the queue and closes what was open. The cache keeps working for tabs that
		// close and reopen — that behaviour predates this class and is not what the setting is
		// about.
		void SetEnabled(bool enabled);

		bool IsEnabled() const { return m_Enabled; }

		// How many files may be outstanding at once. Zero is refused rather than honoured: a limit
		// of zero is a feature that is off, and SetEnabled is what says that.
		void SetConcurrency(std::size_t limit);

		// The width the cached overlays are measured against. A server counts UTF-16 units of the
		// raw line and the editor draws the line with its tabs expanded, so an overlay converted
		// under one width and drawn under another puts its colours on the wrong characters. See
		// SemanticOverlay for the full account of the two coordinate systems.
		void SetTabWidth(std::size_t width);

		// Whether a walk is still finding files. Only the status bar cares: a Total that grows
		// while the pass runs reads as the count going backwards unless the bar can say why.
		void SetWalking(bool walking);

		void Enqueue(std::vector<std::filesystem::path> paths);

		// The folder changed, or the feature was switched off. Drops the queue and closes what was
		// open with the server.
		void Clear();

		// Once a frame on the UI thread. Finishes with whatever has both its answers, gives up on
		// whatever has gone silent for too long, and starts as many new files as the limit leaves
		// room for. `heldElsewhere` answers whether somebody other than this class already has the
		// path open with the server — a tab, a preview pane, anything that sent its own didOpen. A
		// predicate rather than a DocumentSet, so this class needs none of the editor to be
		// tested, and a question about holders rather than about tabs because a preview pane holds
		// no tab and would otherwise have its document closed out from under it.
		//
		// `foregroundBusy` is yielded to, but only for so long: a foreground question that is
		// never answered would otherwise stop this pass for the rest of the session.
		void Tick(double now, bool foregroundBusy,
				  const std::function<bool(const std::filesystem::path&)>& heldElsewhere);

		// True when this answer was one of ours, whether or not it was any use. The caller routes
		// by return value, exactly as it does for PreviewSemantics.
		//
		// Records the answer and nothing more. Finishing with the file — closing it, and deciding
		// whether what came back is worth keeping — happens in Tick, because that decision needs
		// the `heldElsewhere` predicate and an answer arrives without one.
		bool Apply(const LanguageServices::Answer& answer);

		AnalysisProgress Progress() const;

	private:
		// A file that has been asked about and not yet finished.
		struct PendingAnalysis
		{
			std::filesystem::path Path;
			LanguageId Language = LanguageId::PlainText;

			std::filesystem::file_time_type ModifiedAt{};

			// The exact text the requests are about. The answers' columns are only meaningful
			// against it, and it is the only copy of the file this class has — nothing else can
			// convert them.
			Core::TextBuffer Snapshot;

			bool Opened = false;

			// Whether somebody else — a tab, a preview pane — held this file open with the server
			// as of the last Tick. Carried on the entry rather than asked at each of the four
			// places one is torn down — FinishCompleted, the timeout, Clear and the destructor —
			// because two of those have no predicate to ask with: SetEnabled(false) reaches Clear
			// from a settings panel, and a destructor has nobody left to ask at all. One answer,
			// recorded once a frame, is also the only way the four places cannot drift apart,
			// which is how this went wrong the first time.
			//
			// The trade, deliberately: a holder that appears between a frame's Tick and a Clear in
			// that same frame leaves this pass's didOpen un-closed, which leaks one document
			// entry until the server is restarted. That is the harmless direction — the other one
			// takes the document away from a reader that will never open it again.
			bool HeldElsewhere = false;

			// Whether somebody else held this file open with the server at any point since this
			// entry was made. Sticky: set with `|=` once a frame and never cleared, because the
			// question it answers is not the one HeldElsewhere answers and the two need different
			// answers.
			//
			// HeldElsewhere is about *now*: whether a didClose from here would take the document
			// away from a reader who is still using it. That has to decay when the holder goes
			// away, or this class would disown a didOpen nobody else owns any more.
			//
			// This one is about *ever*: whether the answers being retired can still be said to
			// describe the bytes on disk. They cannot, once a holder has existed. The pass opens a
			// file with the text it read from disk, but a tab that opens on the same path sends
			// its own didChange, and from then on the server answers against the tab's buffer —
			// including the questions this class asked before the tab appeared. If the tab then
			// closes before the second answer lands, a live HeldElsewhere is false again and the
			// result looks keepable: declarations describing text that was never on disk, stamped
			// with the disk file's mtime, written to the store and served in the first frame of
			// every tab open until somebody saves the file.
			bool EverHeldElsewhere = false;

			std::optional<LanguageServices::Ticket> Colours;
			std::optional<LanguageServices::Ticket> Symbols;

			bool HaveColours = false;
			bool HaveSymbols = false;

			// What the answers have built so far, stamped and handed to the cache and the store
			// once both are in.
			CachedAnalysis Result;

			double StartedAt = 0.0;
		};

		// Whether this path is already being analysed here — waiting for its text, or waiting for
		// a server's answers.
		//
		// This one line stands where a structure used to. LanguageServices keeps one entry per
		// open document and CloseDocument counts no references (ADR 0512), so two analyses of one
		// path would mean two didOpens and two didCloses for that single entry: the first close
		// takes the document away from the second, whose answers then describe a document the
		// server no longer holds. While this class analysed one file at a time that was impossible
		// by construction; with several outstanding it is impossible only because of this check.
		//
		// Plain equality, and safe to be: every path is normalised as it comes off the queue, so
		// this, the store's lookup and what the server is told all mean the same thing by "the
		// same file".
		bool IsOutstanding(const std::filesystem::path& path) const;

		// Takes paths off the queue until the limit is reached or the queue runs out.
		void StartNext(const std::function<bool(const std::filesystem::path&)>& heldElsewhere);

		// Takes the predicate too: the read is a round trip through a worker, so a tab or a pane
		// can claim the file between the frame its text was asked for and the frame it arrives.
		void BeginArrivedFiles(
			double now, const std::function<bool(const std::filesystem::path&)>& heldElsewhere);

		// Retires everything with both its answers in, and gives up on everything that has gone
		// silent for too long. One walk, because both decisions are per entry: an entry carries
		// its own StartedAt, so one timing out has nothing to do with the others.
		void FinishCompleted(double now);

		// Drops the entry at `index`, telling the server the file is closed only if this class is
		// still the one holding it open. See PendingAnalysis::HeldElsewhere.
		void Drop(std::size_t index);

		void DropAll();

		WorkspaceAnalysisPort& m_Port;
		FileTextSource& m_Texts;
		AnalysisCache& m_Cache;

		// Null when no folder is open, and asked before every read: a file the store already has
		// is what makes a second launch cheap.
		AnalysisStore* m_Store = nullptr;

		bool m_Enabled = true;
		bool m_Walking = false;
		std::size_t m_TabWidth = Core::DefaultTabWidth;

		std::map<LanguageId, std::deque<std::filesystem::path>> m_Queues;
		std::size_t m_Done = 0;
		std::size_t m_Queued = 0;

		// At most m_Limit of these exist at once. A vector rather than a map: the limit is small
		// enough that finding one by path or by ticket is a scan of a handful of entries, and the
		// alternative is a second structure to keep in step with this one.
		std::vector<PendingAnalysis> m_Pending;

		// Paths whose text has been asked for and not yet arrived. Not pending analyses yet:
		// nothing has been said to a server, so there is nothing to close and nothing to time out.
		std::vector<std::filesystem::path> m_AwaitingText;

		// How many files may be outstanding at once — pending analyses and awaited reads together,
		// since a read on its way is a slot already spoken for. Never zero: a limit of zero is a
		// feature that is off, and that is what SetEnabled is for.
		std::size_t m_Limit = 1;

		// When the open document's unbroken run of being busy began, so the yield to it can be
		// bounded. Empty on every frame it is not busy. See kForegroundYieldCapSeconds.
		std::optional<double> m_BusySince;

		// Kept as a member rather than made afresh each frame: Tick runs sixty times a second and
		// this is the one buffer in the class that would otherwise reallocate for every one of
		// them.
		std::vector<FileText> m_Arrived;
	};
}

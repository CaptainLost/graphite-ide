#pragma once

#include "Lang/Analysis/FileTextSource.h"
#include "Lang/Api/LanguageId.h"
#include "Lang/Server/LanguageServices.h"

#include "Editor/AnalysisCache.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What WorkspaceAnalysis needs from a language server and nothing more.
	//
	// Separate from LanguageServices for the reason PreviewServerPort is: the bookkeeping above
	// it — one file at a time, yield to the open document, cache what came back — is worth
	// asserting without a clangd process.
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
	};

	// How far the pass has got, for the status bar. Total is what was queued, not what the tree
	// holds: until the walk finishes there is no such number, which is what Walking says.
	struct AnalysisProgress
	{
		std::size_t Done = 0;
		std::size_t Total = 0;
		bool Walking = false;
	};

	// Asks the server about every file in the workspace, one at a time, so that opening one paints
	// in the frame it opens rather than after a round trip.
	//
	// The whole design is one sentence: never more than one file open with the server at a time,
	// and never at the expense of the file someone is looking at. The first is what keeps the
	// server's footprint independent of the size of the tree — a didOpen pins a preamble and an
	// AST, and a queue that opened a hundred files would cost a hundred of them. The second is
	// what keeps the feature from being felt: a background file's answers are worth nothing if
	// the active document's are behind them.
	//
	// Results go into AnalysisCache, which DocumentWindows already reads when a document opens.
	// Nothing here draws, and nothing here is a source of truth: what it produces is a first
	// frame, overwritten by the server's own answer as soon as a tab asks its own question.
	//
	// Nothing here reads a file or waits for anything either. Text arrives through a
	// FileTextSource, which does the reading on a worker; every method below runs on the UI
	// thread and returns inside a frame.
	class WorkspaceAnalysis
	{
	public:
		WorkspaceAnalysis(WorkspaceAnalysisPort& port, FileTextSource& texts, AnalysisCache& cache);

		// Closes whatever it had open. A flight outliving this object would leave a document open
		// with a server for the rest of the session.
		~WorkspaceAnalysis();

		WorkspaceAnalysis(const WorkspaceAnalysis&) = delete;
		WorkspaceAnalysis& operator=(const WorkspaceAnalysis&) = delete;

		// False drops the queue and closes what was open. The cache keeps working for tabs that
		// close and reopen — that behaviour predates this class and is not what the setting is
		// about.
		void SetEnabled(bool enabled);

		bool IsEnabled() const { return m_Enabled; }

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

		// Once a frame on the UI thread. Retires a finished flight and launches at most one new
		// one. `hasTab` answers whether a path is open in the editor — a predicate rather than a
		// DocumentSet, so this class needs none of the editor to be tested.
		void Tick(double now, bool foregroundBusy,
				  const std::function<bool(const std::filesystem::path&)>& hasTab);

		// True when this answer was one of ours, whether or not it was any use. The caller routes
		// by return value, exactly as it does for PreviewSemantics.
		//
		// Records the answer and nothing more. Retiring the file — closing it, and deciding
		// whether what came back is worth keeping — happens in Tick, because that decision needs
		// the `hasTab` predicate and an answer arrives without one.
		bool Apply(const LanguageServices::Answer& answer);

		AnalysisProgress Progress() const;

	private:
		// A file that has been asked about and not yet finished. At most one exists.
		struct Flight
		{
			std::filesystem::path Path;
			LanguageId Language = LanguageId::PlainText;

			std::filesystem::file_time_type ModifiedAt{};

			// The exact text the requests are about. The answers' columns are only meaningful
			// against it, and it is the only copy of the file this class has — nothing else can
			// convert them.
			Core::TextBuffer Snapshot;

			bool Opened = false;

			// Whether a tab held this file as of the last Tick. Carried on the flight rather than
			// asked at each of the four places a flight is torn down — Retire, the timeout,
			// Clear and the destructor — because two of those have no predicate to ask with:
			// SetEnabled(false) reaches Clear from a settings panel, and a destructor has nobody
			// left to ask at all. One answer, recorded once a frame, is also the only way the
			// four places cannot drift apart, which is how this went wrong the first time.
			//
			// The trade, deliberately: a tab that appears between a frame's Tick and a Clear in
			// that same frame leaves this pass's didOpen un-closed, which leaks one document
			// entry until the server is restarted. That is the harmless direction — the other one
			// takes the document away from a tab that will never open it again.
			bool TabTook = false;

			std::optional<LanguageServices::Ticket> Colours;
			std::optional<LanguageServices::Ticket> Symbols;

			bool ColoursIn = false;
			bool SymbolsIn = false;

			// What the answers have built so far, stamped and handed to the cache once both are
			// in.
			CachedAnalysis Result;

			double StartedAt = 0.0;
		};

		void Launch(const std::function<bool(const std::filesystem::path&)>& hasTab);
		void CollectTexts(double now);
		void Retire();

		// Drops the flight, telling the server the file is closed only if this class is still the
		// one holding it open. See Flight::TabTook.
		void CloseFlight();

		WorkspaceAnalysisPort& m_Port;
		FileTextSource& m_Texts;
		AnalysisCache& m_Cache;

		bool m_Enabled = true;
		bool m_Walking = false;
		std::size_t m_TabWidth = Core::DefaultTabWidth;

		std::deque<std::filesystem::path> m_Queue;
		std::size_t m_Done = 0;
		std::size_t m_Queued = 0;

		// Set between asking the source for a file's text and the text arriving. Not a Flight yet:
		// nothing has been said to a server, so there is nothing to close and nothing to time out.
		std::optional<std::filesystem::path> m_Reading;

		std::optional<Flight> m_Flight;

		// Kept as a member rather than made afresh each frame: Tick runs sixty times a second and
		// this is the one buffer in the class that would otherwise reallocate for every one of
		// them.
		std::vector<FileText> m_Arrived;
	};
}

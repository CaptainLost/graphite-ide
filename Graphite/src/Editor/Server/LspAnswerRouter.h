#pragma once

#include "Editor/Server/DocumentServerPort.h"

#include "Language/Api/LanguageService.h"
#include "Language/Server/LanguageServices.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace Graphite {

	// Where a document's caret was last drawn on screen, kept so a request that began at the
	// keyboard - F12 never touches the mouse the way Ctrl+click does - still has somewhere on
	// screen to open a popup near. One frame stale at worst: the value from the document's own
	// last draw, read back before that document draws again.
	struct CaretScreenPosition
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	// What a definition question was asked from, carried untouched to its answer.
	//
	// The caret's offset at the moment of asking stands in for a generation: nothing outside an
	// edit moves it on its own, so an answer that finds a different offset there now is about a
	// question the caret has since moved on from, and jumping on its strength would read as the
	// editor jumping unprompted.
	struct DefinitionQuestion
	{
		std::filesystem::path Path;
		std::size_t CaretOffset = 0;
		FilePosition From;
		CaretScreenPosition Screen;
	};

	// What a hover or preview question was asked about, carried to its answer so an answer for a
	// token the mouse has left, or for text that has since changed, can be told apart.
	struct HoverQuestion
	{
		std::filesystem::path Path;
		std::size_t DocumentRevision = 0;
		std::size_t Line = 0;
		std::size_t TokenFrom = 0; // raw byte offsets within the line
		std::size_t TokenTo = 0;
		FilePosition From;

		friend bool operator==(const HoverQuestion&, const HoverQuestion&) = default;
	};

	// Where LspAnswerRouter delivers what it routed. Each method is reached only for an answer to
	// a question still outstanding: a superseded, cancelled or forgotten ticket never gets here.
	class LspAnswerTarget
	{
	public:
		virtual ~LspAnswerTarget() = default;

		LspAnswerTarget() = default;
		LspAnswerTarget(const LspAnswerTarget&) = delete;
		LspAnswerTarget& operator=(const LspAnswerTarget&) = delete;

		// Failed answers included, so the question can still be said to have no answer.
		virtual void ApplyDefinition(const DefinitionQuestion& question,
									 const LanguageServices::Answer& answer) = 0;
		virtual void ApplyReferences(const std::string& symbol,
									 const LanguageServices::Answer& answer) = 0;

		// Failed answers included, and an answer naming no file: either way there is nowhere to
		// go, and the target decides whether that is worth saying.
		virtual void ApplySwitchSourceHeader(const std::filesystem::path& from,
											 const LanguageServices::Answer& answer) = 0;

		// Failed answers included, like ApplyDefinition: the target decides what a hover with no
		// answer looks like.
		virtual void ApplyHover(const HoverQuestion& question,
								const LanguageServices::Answer& answer) = 0;
		virtual void ApplyPreviewDefinition(const HoverQuestion& question,
											const LanguageServices::Answer& answer) = 0;

		// Successful answers only: a failure has nothing to apply, and the router has already
		// done the bookkeeping it calls for.
		virtual void ApplySemanticTokens(const std::filesystem::path& path,
										 const LanguageServices::Answer& answer) = 0;

		// Whether the document took them. False for a document with nothing to anchor them to,
		// which leaves them wanted.
		virtual bool ApplySymbols(const std::filesystem::path& path,
								  const LanguageServices::Answer& answer) = 0;

		virtual void ApplyCount(const std::filesystem::path& path, std::size_t documentRow,
								std::size_t count) = 0;

		// An answer to a question the router did not ask - a preview pane's colours, the
		// background pass's - offered to whoever did. Whether anyone took it.
		virtual bool Offer(const LanguageServices::Answer& answer) = 0;
	};

	// The questions the editor has outstanding with the language servers, and where each answer
	// goes when it arrives.
	//
	// Pulled out of DocumentWindows so the rules about tickets can be asserted with no window and
	// no server: a second F12 supersedes the first, a failed answer still clears what was waiting
	// on it, a server that restarts is asked again, and declarations wanted but refused are asked
	// for again on a clock rather than every frame. DocumentWindows owns one, drives it once a
	// frame per served document, and applies what it routes.
	class LspAnswerRouter
	{
	public:
		// Seconds, the unit ImGui::GetTime keeps; injected so the retry clock can be stepped in a
		// test.
		using Clock = std::function<double()>;

		// How long before a symbols request that came back Failed is tried again. A server is
		// routinely still starting or still indexing when the first one goes out, and its answer
		// then says nothing about whether a later one would succeed - but retrying on the next
		// frame would be sixty requests a second at a server that is busy.
		static constexpr double kSymbolsRetrySeconds = 2.0;

		LspAnswerRouter(DocumentServerPort& server, Clock clock);

		// Starts following a document a server serves: its declarations are wanted from now on.
		// Called once, when the document is opened with its server.
		void Track(const std::filesystem::path& path);

		// Stops following a document that has gone, and drops the tickets outstanding for it, so
		// nothing waits on answers nobody will read. The questions themselves are not cancelled
		// with the server.
		void Forget(const std::filesystem::path& path);

		// Once a frame per followed document: reads the state of the server serving it and acts
		// on a change. A server that has just finished starting is asked for colours again; a
		// server that has just been replaced is asked for everything again, and the counts it was
		// asked for are dropped. True in that second case, so the caller can let the rows it had
		// already asked about be asked about again.
		bool NoteServerState(const std::filesystem::path& path);

		// The document's text has moved: its declarations are wanted again, at once rather than
		// on the retry clock.
		void WantSymbolsNow(const std::filesystem::path& path);

		// Asks for declarations and colours together when they are wanted, none is outstanding,
		// and the retry clock has run out.
		void AskSymbolsIfDue(const std::filesystem::path& path);

		void AskSemanticTokens(const std::filesystem::path& path);

		// The reference count for the declaration a lens on `documentRow` sits above.
		void AskCount(const std::filesystem::path& path, std::size_t documentRow,
					  const FilePosition& where);

		// Cancels every count still outstanding for this document: they were asked about rows
		// whose text has since moved.
		void DropCounts(const std::filesystem::path& path);

		// At most one of each outstanding at a time: asking again cancels the question still
		// waiting, because only the most recent one is something the user is waiting on. Starts
		// the server first, as the question may be the first thing asked of it.
		void AskDefinition(DefinitionQuestion question);
		void AskReferences(const std::filesystem::path& path, const FilePosition& where,
						   std::string symbol);

		// At most one outstanding, like AskDefinition: a second Alt+O supersedes the first.
		void AskSwitchSourceHeader(const std::filesystem::path& path);

		// A hover and a preview's definition each have a slot of their own, so neither cancels
		// F12 or the other. Asking again supersedes only the question in the same slot.
		void AskHover(HoverQuestion question);
		void AskPreviewDefinition(HoverQuestion question);

		// Cancels both slots when they hold this document's questions: the mouse has left, and
		// their answers are about a place it is not. Another document's question is left alone —
		// the slots are shared, and one editor's leave grace running out says nothing about the
		// question a second editor beside it has just asked.
		void CancelHover(const std::filesystem::path& path);

		// Whether this document's declarations or colours are still being settled: wanted, or
		// asked for and not yet answered. Reference counts deliberately do not count; see
		// DocumentWindows::IsAnalysing.
		bool IsAnalysing(const std::filesystem::path& path) const;

		// Takes everything the servers have answered and routes each answer to `target`. The only
		// place answers are collected: DocumentServerPort::TakeAnswers is the single drain point.
		void Collect(LspAnswerTarget& target);

	private:
		// Symbols are wanted from the moment the document opens and again after every edit. One
		// request may be outstanding at a time, and a failed one is retried on the clock.
		struct Schedule
		{
			bool Want = true;
			bool Outstanding = false;
			double NextAt = 0.0;

			// What the serving server's state was the last frame this document drew, so a
			// restart is noticed. The client that died failed everything it held outstanding, so
			// without this the rows on screen at the moment of a crash keep a blank count for the
			// rest of the session, however healthy the replacement server is.
			ServerState LastServerState = ServerState::NotConfigured;
		};

		struct PendingDefinition
		{
			LanguageServices::Ticket Ticket{};
			DefinitionQuestion Question;
		};

		struct PendingReferences
		{
			LanguageServices::Ticket Ticket{};
			std::string Symbol;
		};

		struct PendingSwitch
		{
			LanguageServices::Ticket Ticket{};
			std::filesystem::path From;
		};

		struct PendingHover
		{
			LanguageServices::Ticket Ticket{};
			HoverQuestion Question;
		};

		struct PendingCount
		{
			std::filesystem::path Path;
			std::size_t DocumentRow = 0;
		};

		DocumentServerPort& m_Server;
		Clock m_Clock;

		std::map<std::filesystem::path, Schedule> m_Schedules;

		std::optional<PendingDefinition> m_PendingDefinition;
		std::optional<PendingReferences> m_PendingReferences;
		std::optional<PendingSwitch> m_PendingSwitch;
		std::optional<PendingHover> m_PendingHover;
		std::optional<PendingHover> m_PendingPreview;

		// What an outstanding ticket was asked for. Tickets are unique across every server, which
		// is what lets one map per question kind route an answer back to the document - and to
		// the row - that wanted it, without either server's numbering colliding with the other's.
		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSymbols;
		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSemantic;
		std::map<LanguageServices::Ticket, PendingCount> m_PendingCounts;
	};
}

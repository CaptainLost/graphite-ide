#pragma once

#include "Core/Base/Base.h"

#include "Core/Platform/ChildProcess.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/JsonRpc.h"
#include "Language/Server/LspProtocol.h"
#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/ServerLaunch.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace Graphite {

	// One conversation with one language server: it starts the process, keeps the server's copy
	// of the open documents in step, asks three questions, and hands the answers back to the UI
	// thread through Drain — the same division SearchService draws, for the same reason. A
	// background thread reads the server's pipe and blocks on it; the UI thread must never wait
	// on a server that may be indexing for a minute, so nothing here blocks on a reply.
	class LspClient
	{
	public:
		LspClient(const ServerLaunch& launch, std::filesystem::path root);

		// Tags the constructor that does everything except start a process, so that the paths only
		// a server can otherwise reach — the handshake, the queue that holds messages until it
		// finishes, the answers to a server's own requests — are reachable from a test on a
		// machine with no language server installed. Nothing in the application uses it.
		struct WithoutServer
		{
		};

		LspClient(const ServerLaunch& launch, std::filesystem::path root, WithoutServer);

		~LspClient();

		LspClient(const LspClient&) = delete;
		LspClient& operator=(const LspClient&) = delete;

		// Each returns the version it sent the document under, which is the only thing that can
		// later say whether an answer describes the text on screen. ChangeDocument returns a
		// default-constructed version for a document the server was never told about, which is
		// the same "nothing was sent" its early-out has always meant but never said.
		DocumentVersion OpenDocument(const std::filesystem::path& path, const std::string& text);
		DocumentVersion ChangeDocument(const std::filesystem::path& path, const std::string& text);
		void SaveDocument(const std::filesystem::path& path);
		void CloseDocument(const std::filesystem::path& path);

		// Each returns a ticket; the answer arrives through Drain.
		RequestId AskDefinition(const FilePosition& where);
		RequestId AskReferences(const FilePosition& where);

		// Answered in Answer::Text, as the markdown clangd sends; empty when it has nothing to say.
		RequestId AskHover(const FilePosition& where);

		// clangd's extension; answered with the counterpart file as a single location, or with
		// no location when there is none.
		RequestId AskSwitchSourceHeader(const std::filesystem::path& path);
		RequestId AskSymbols(const std::filesystem::path& path);

		// Answers nothing at all when the server declared no semantic token support in its
		// handshake: there is no legend to read a reply through, so asking would be asking for
		// integers nobody can interpret. Returns 0 in that case, which no request ever uses.
		RequestId AskSemanticTokens(const std::filesystem::path& path);

		struct Answer
		{
			RequestId Id = 0;
			std::vector<Location> Locations;
			std::vector<Declaration> Declarations;
			std::vector<SemanticToken> SemanticTokens;
			// The hover reply's markdown; empty for every other question, and for a hover with
			// nothing to say.
			std::string Text;
			bool Failed = false;
		};

		// How long a server gets to answer `initialize` before it is called Failed. clangd answers
		// the handshake before it indexes anything — indexing is reported afterwards through
		// $/progress — so a healthy one answers in well under a second even on a cold, large
		// project, and this is generous enough to survive a slow process start on a machine
		// scanning the binary as it launches. It is also short enough that a server which starts
		// and then says nothing is restarted while the user is still looking at the file that
		// prompted it, rather than leaving the status bar reading "starting" for the session.
		static constexpr std::chrono::seconds kHandshakeTimeout{30};

		// Fails the handshake when `now` is past its deadline: without this a server that starts
		// and never answers leaves Initialized false forever, every notification queued behind a
		// reply that is not coming, and LanguageServices's restart policy — which acts on Failed —
		// never gets its chance. Called by Drain with the real clock, so the check costs two
		// comparisons a frame and needs no timer thread; a test passes its own clock rather than
		// spending the timeout waiting.
		void ExpireHandshakeIfStalled(std::chrono::steady_clock::time_point now);

		// How many messages are held back waiting for the handshake. A test's window onto a queue
		// that is otherwise invisible from out here — the leak it guards against has no symptom
		// until the process runs out of memory.
		std::size_t QueuedCount() const;

		// Everything one client has for the UI thread since the last call. A struct rather than
		// three out-parameters: two of these three - the status and the diagnostics - arrive with
		// no RequestId to travel on, and a parameter list that grows once per unticketed message
		// kind is the one ADR 0034 asks to be replaced by the type it is spelling out.
		struct Drained
		{
			std::vector<Answer> Answers;
			ServerStatus Status;
			std::vector<PublishedDiagnostics> Diagnostics;
			std::vector<ServerMessage> Messages;
		};

		// UI thread only. Moves everything the server has said since the last call into
		// `drained` and reports where the server is. Also expires a handshake that has run out
		// of time, which is what makes that check cost nothing extra: this already runs once a
		// frame.
		void Drain(Drained& drained);

		void Cancel(RequestId id);

		// Sends "shutdown" then "exit", queued the same way any other message is and not waited
		// on — this class has no way to know whether a server is well enough to answer, and
		// waiting here would be the UI thread blocking on a process that may be wedged. Owned by
		// whoever manages this client's lifetime, called right before destroying it; the
		// destructor's Terminate is the backstop for a server that ignores both.
		void RequestShutdown();

		// One message body, handled exactly as the reader thread handles what arrives on the pipe.
		// The reader calls this too, so a test that feeds a body drives the same code a server
		// does — there is no second path that could drift from the real one.
		void Deliver(const std::string& body);

		// Everything queued for the server since the last call, framed and in order. Only a client
		// with no process to drain the queue accumulates anything here, which is what makes it a
		// test's window onto what would have been sent.
		std::vector<std::string> TakeSent();

	private:
		// What a reply id was asked for. The protocol does not echo the method back, so the
		// question has to be remembered against its id to know how to read the answer.
		enum class Question : std::uint8_t
		{
			Definition,
			References,
			Symbols,
			SemanticTokens,
			SwitchSourceHeader,
			Hover
		};

		// The server's copy of an open document, as far as this side knows it: the version, because
		// the protocol requires it to increase per document, and the path it was opened under. The
		// text is not kept — the client has no reason to measure anything against it any more, and
		// a second copy of every open file is not free.
		struct Document
		{
			int Version = 0;

			// Paths compare by spelling, drive letter included, so anything reported about this document must carry this path rather than one read back from a URI.
			std::filesystem::path Path;
		};

		// What was asked under an id. The protocol does not echo the method back, so the question
		// has to be remembered against its id to know how to read the answer.
		struct Asked
		{
			Question Kind = Question::Definition;

			// Where the question was asked from, for the questions that have a position. A
			// references reply names it among its answers — a declaration is a reference to
			// itself as far as the protocol is concerned — and nobody wants to be shown the
			// line they are standing on, so it is dropped on the way out. Kept here because
			// this is the only place that still knows it by the time the reply arrives.
			std::optional<FilePosition> From;
		};

		// Held through a shared_ptr so a reader thread still winding down after this object is
		// gone has somewhere valid to write, exactly as SearchService's jobs do.
		//
		// Three locks, and which covers what matters more than any of them individually — the
		// deadlock this shape exists to prevent is one where the UI thread waits to write while
		// the server waits for us to read. Add a message and reach for the right one:
		//
		//   StateMutex     Answers, Status, Diagnostics and Messages. What Drain collects, and nothing else.
		//   ProtocolMutex  Bookkeeping only: id allocation, the outstanding table, the document
		//                  versions, the pre-handshake queue, InitializeId, Initialized,
		//                  ReaderStopped. Never held across any I/O whatsoever.
		//   OutboundMutex  The outbound queue and nothing else. Held to push or to pop, never
		//                  across the write itself.
		//
		// When two are held at once ProtocolMutex is always the outer one: StateMutex or
		// OutboundMutex may be taken inside it, never the other way round, and those two are never
		// held together.
		//
		// Nothing ever writes to the pipe while holding a lock: a synchronous write into a full
		// pipe blocks until the server reads, the server does not read until we read, and our
		// reader cannot read while it is waiting on a lock the blocked writer holds. The writer
		// thread exists to break exactly that cycle — every sender pushes and returns.
		struct Shared
		{
			std::mutex StateMutex;
			std::vector<Answer> Answers;
			ServerStatus Status;
			std::vector<PublishedDiagnostics> Diagnostics;
			std::vector<ServerMessage> Messages;

			// The $/progress tokens that have begun and not yet ended. A server may run several at
			// once, and Indexing ends only when the last of them does - the first to finish says
			// nothing about the rest. Kept as JSON because a token is an integer or a string and
			// the two never name the same token. Under StateMutex, with the Status it decides.
			std::vector<nlohmann::json> ProgressTokens;

			// Under StateMutex, like ProgressTokens: the server is not settled while either says it is still busy.
			std::optional<std::string> ProjectsLoading;

			std::mutex ProtocolMutex;
			JsonRpc Rpc;
			std::unordered_map<RequestId, Asked> Pending;
			std::unordered_map<std::string, Document> Documents;

			// The last version number this client ever sent for a URI, kept even after the
			// document closes and its Documents entry is erased. OpenDocument reads it so a
			// reopened document continues the same numbering rather than starting back at 1 -
			// see OpenDocument's own comment for why restarting it there is a bug. Never erased:
			// only a genuinely new LspClient - what a server restart constructs - starts this
			// map, and therefore the numbering, over.
			std::unordered_map<std::string, int> LastVersions;
			std::vector<std::string> Queued;
			RequestId InitializeId = 0;
			bool Initialized = false;

			// What the server said its semantic token numbering means, read once from the
			// handshake. Empty when it offers none, which is what AskSemanticTokens checks before
			// asking for integers nobody could interpret. Under ProtocolMutex with the rest of the
			// bookkeeping.
			std::vector<SemanticKind> SemanticLegend;

			// The handshake will never finish: initialize was answered with an error, it ran out
			// of time, or there was never a process to answer it. Initialized stays false in all
			// three cases, so without this every notification sent afterwards would be appended to
			// Queued and left there — a whole copy of the file per keystroke, forever, at a server
			// nothing will ever hand it to. Under ProtocolMutex, like Initialized itself.
			bool HandshakeFailed = false;

			// When to give up waiting for the initialize reply. Set by BeginHandshake and read on
			// the UI thread by ExpireHandshakeIfStalled; left at the epoch when no handshake was
			// ever sent, which HandshakeFailed already covers.
			std::chrono::steady_clock::time_point HandshakeDeadline{};

			// Set by the reader as it exits, under ProtocolMutex and in the same breath as it
			// abandons what was outstanding, so a question asked at that moment is either
			// remembered by a reader still running or failed on the spot — never neither.
			bool ReaderStopped = false;

			std::mutex OutboundMutex;
			std::condition_variable Outbound;
			std::vector<std::string> ToSend;

			// Set by the writer as it stops, which is the moment the queue stops being a queue and
			// becomes a leak: requests fail fast through ReaderStopped, but notifications do not,
			// and a session that keeps typing at a dead server would pile up a whole file's text
			// per keystroke forever. From here on messages are dropped instead of appended. The
			// live path stays unbounded on purpose — a push that can never block is what keeps the
			// reader off the writer's back.
			bool WriterStopped = false;

			// Set before the child is killed, so both threads can tell a shutdown they were asked
			// for from a server that died on its own.
			std::atomic<bool> Stopping{false};

			// Written once, before either thread starts, and only read afterwards.
			Core::Ref<Core::ChildProcess> Process;
			std::string ProtocolLanguageId;
			const ServerAdapter* Adapter = nullptr;
			AdapterContext Context;
			std::optional<ProjectProblem> Problem;
		};

		// Numbers a question, remembers what was asked so its reply can be read, and queues it.
		// ProtocolMutex held by the caller.
		RequestId Ask(std::string_view method, const nlohmann::json& params, Question question,
					  std::optional<FilePosition> from = std::nullopt);

		// Sends initialize and records the id its reply will carry. ProtocolMutex NOT held.
		void BeginHandshake(const std::filesystem::path& root);

		void Adopt(const ServerLaunch& launch, const std::filesystem::path& root);

		// StateMutex held by the caller.
		static void Settle(Shared& shared);

		// Everything below runs on a background thread and takes Shared explicitly, because by the
		// time some of it runs there may be no LspClient left to reach through.
		static void RunReader(Core::Ref<Shared> shared);
		static void RunWriter(Core::Ref<Shared> shared);

		// Decides what one body is and hands it to the function for that kind of message: a
		// server's request, the handshake's reply, a notification, or the reply to a question.
		// The handshake's reply is told apart from the others because it answers no question - it
		// carries the legend and releases the queue.
		static void HandleBody(Shared& shared, const std::string& body);

		// Both are called with ProtocolMutex held.
		static void AnswerServerRequest(Shared& shared, const IncomingMessage& request);
		static void CompleteHandshake(Shared& shared, const IncomingMessage& reply);

		// Neither is called with a lock held; each takes what it needs.
		static void HandleNotification(Shared& shared, const IncomingMessage& notification);
		static void HandleReply(Shared& shared, const IncomingMessage& reply);

		static void PublishDiagnostics(Shared& shared, const nlohmann::json& params);

		// ProtocolMutex held by the caller.
		static void NameOpenDocumentsAsOpened(const Shared& shared, std::vector<Location>& locations);

		// Takes StateMutex itself, so it is called with no lock held.
		static void RecordServerMessage(Shared& shared, const nlohmann::json& params);
		static void ApplyProgress(Shared& shared, const nlohmann::json& params);

		// Reads a successful or failed reply as the answer to what was asked under its id. Pure:
		// it touches no shared state beyond the legend it is handed.
		static Answer ReadAnswer(const Asked& asked, const IncomingMessage& reply,
								 const std::vector<SemanticKind>& legend);

		// Both hand a framed message to the writer thread and return without waiting for it to
		// leave, and both need ProtocolMutex held by the caller — not to write, but because they
		// read Initialized and because the order two messages are queued in has to be the order
		// their ids were allocated in. SendLocked holds a message back until the handshake has
		// finished; SendNowLocked is for the handshake itself and for answers a server is waiting
		// on, which must overtake the queue rather than sit behind it.
		//
		// Both take the message by value and move it onward rather than taking a reference: what
		// arrives is a framed message nobody else keeps, and the only thing either does with it is
		// hand it to a queue. Taken by reference it was copied into that queue, which for a
		// didChange is one more whole copy of the source file, on the UI thread, per keystroke.
		static void SendLocked(Shared& shared, std::string bytes);
		static void SendNowLocked(Shared& shared, std::string bytes);

		// Declares the outbound queue closed and empties it. Called by whichever of the two
		// threads first learns the pipe is dead, so that senders start dropping rather than
		// piling up messages nothing will ever carry. Takes OutboundMutex.
		static void StopOutbound(Shared& shared);

		// Ends the server process and joins whichever of the two threads were started. What the
		// destructor does, shared with a constructor that fails between starting them.
		void StopThreads();

		Core::Ref<Shared> m_Shared = Core::CreateRef<Shared>();

		// Declared last so they are destroyed last; the destructor joins both explicitly first.
		std::thread m_Reader;
		std::thread m_Writer;
	};
}

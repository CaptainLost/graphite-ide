#pragma once

#include "Core/Platform/ChildProcess.h"
#include "Lang/Api/LanguageService.h"
#include "Lang/Server/JsonRpc.h"
#include "Lang/Server/ServerRegistry.h"

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

	// The boundary conversions and the reply parsers, free because they are the whole of what
	// this client does that can be checked without a process. Everything below them is plumbing
	// around these six functions.

	// A path as the protocol spells it: forward slashes, a file:/// prefix, and percent-encoding
	// for everything outside the unreserved set. The encoding is not cosmetic — clangd compares
	// URIs as strings, so a path that reaches it under two different spellings is two different
	// files to it, and every answer about the second one comes back empty.
	std::string ToFileUri(const std::filesystem::path& path);
	std::filesystem::path FromFileUri(std::string_view uri);

	// The protocol counts columns in UTF-16 code units and this editor counts bytes. Both
	// conversions walk `line`'s UTF-8 lead bytes, counting one unit per code point below U+10000
	// and two at or above — a code point at or above U+10000 is four bytes here and a surrogate
	// pair there. A column past the end of the line clamps to its end rather than running off it.
	//
	// Neither is called by LspClient, deliberately. The caller converts, because the caller holds
	// the buffer the column belongs to: the client has only the file on disk, and for a document
	// with unsaved edits that is a different file, so converting here would answer confidently
	// about text the user is not looking at. There is still exactly one pair of these functions,
	// and they are the only way to obtain or spend a Utf16Column.
	Utf16Column ToUtf16Column(std::string_view line, std::size_t byteColumn);
	std::size_t FromUtf16Column(std::string_view line, Utf16Column column);

	// Both tolerate null, a wrong type and missing fields by yielding less, never by failing — a
	// server is allowed to answer nothing, and ADR 0004 forbids throwing across this boundary.
	std::vector<Location> ParseLocations(const nlohmann::json& result);

	// Keeps only the symbol kinds this editor has somewhere to show — functions and types — and
	// flattens the tree, parents before children, so a caller never walks it again. Anything else
	// is skipped rather than stored as DeclarationKind::Other, so no lens appears over a variable.
	std::vector<Declaration> ParseSymbols(const nlohmann::json& result);

	// One conversation with one language server: it starts the process, keeps the server's copy
	// of the open documents in step, asks three questions, and hands the answers back to the UI
	// thread through Drain — the same division SearchService draws, for the same reason. A
	// background thread reads the server's pipe and blocks on it; the UI thread must never wait
	// on a server that may be indexing for a minute, so nothing here blocks on a reply.
	class LspClient
	{
	public:
		LspClient(const ServerConfig& config, std::filesystem::path binary,
				  std::filesystem::path root);

		// Tags the constructor that does everything except start a process, so that the paths only
		// a server can otherwise reach — the handshake, the queue that holds messages until it
		// finishes, the answers to a server's own requests — are reachable from a test on a
		// machine with no language server installed. Nothing in the application uses it.
		struct WithoutServer
		{
		};

		LspClient(const ServerConfig& config, std::filesystem::path root, WithoutServer);

		~LspClient();

		LspClient(const LspClient&) = delete;
		LspClient& operator=(const LspClient&) = delete;

		void OpenDocument(const std::filesystem::path& path, const std::string& text);
		void ChangeDocument(const std::filesystem::path& path, const std::string& text);
		void SaveDocument(const std::filesystem::path& path);
		void CloseDocument(const std::filesystem::path& path);

		// Each returns a ticket; the answer arrives through Drain.
		RequestId AskDefinition(const FilePosition& where);
		RequestId AskReferences(const FilePosition& where);
		RequestId AskSymbols(const std::filesystem::path& path);

		struct Answer
		{
			RequestId Id = 0;
			std::vector<Location> Locations;
			std::vector<Declaration> Declarations;
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

		// UI thread only. Moves everything answered since the last call into `answers` and
		// reports where the server is. Also expires a handshake that has run out of time, which is
		// what makes that check cost nothing extra: this already runs once a frame.
		void Drain(std::vector<Answer>& answers, ServerStatus& status);

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
			Symbols
		};

		// The server's copy of an open document, as far as this side knows it: only the version,
		// because the protocol requires it to increase per document. The text is not kept — the
		// client has no reason to measure anything against it any more, and a second copy of every
		// open file is not free.
		struct Document
		{
			int Version = 0;
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
		//   StateMutex     Answers and Status. What Drain collects, and nothing else.
		//   ProtocolMutex  Bookkeeping only: id allocation, the outstanding table, the document
		//                  versions, the pre-handshake queue, InitializeId, Initialized,
		//                  ReaderStopped. Never held across any I/O whatsoever.
		//   OutboundMutex  The outbound queue and nothing else. Held to push or to pop, never
		//                  across the write itself.
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

			std::mutex ProtocolMutex;
			JsonRpc Rpc;
			std::unordered_map<RequestId, Asked> Pending;
			std::unordered_map<std::string, Document> Documents;
			std::vector<std::string> Queued;
			RequestId InitializeId = 0;
			bool Initialized = false;

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
			std::shared_ptr<Core::ChildProcess> Process;
			std::string ProtocolLanguageId;

			// Decided once at construction: a root with no compile database is a state the user
			// has to fix, not a failure, and it outlives every reply that would otherwise report
			// the server simply Ready.
			bool NoCompileDatabase = false;
		};

		// Numbers a question, remembers what was asked so its reply can be read, and queues it.
		// ProtocolMutex held by the caller.
		RequestId Ask(std::string_view method, const nlohmann::json& params, Question question,
					  std::optional<FilePosition> from = std::nullopt);

		// Sends initialize and records the id its reply will carry. ProtocolMutex NOT held.
		void BeginHandshake(const std::filesystem::path& root);

		// Everything below runs on a background thread and takes Shared explicitly, because by the
		// time some of it runs there may be no LspClient left to reach through.
		static void RunReader(std::shared_ptr<Shared> shared);
		static void RunWriter(std::shared_ptr<Shared> shared);
		static void HandleBody(Shared& shared, const std::string& body);

		// Both are called with ProtocolMutex held.
		static void AnswerServerRequest(Shared& shared, const IncomingMessage& request);
		static void CompleteHandshake(Shared& shared, const IncomingMessage& reply);

		static void ApplyProgress(Shared& shared, const nlohmann::json& params);

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

		std::shared_ptr<Shared> m_Shared = std::make_shared<Shared>();

		// Declared last so they are destroyed last; the destructor joins both explicitly first.
		std::thread m_Reader;
		std::thread m_Writer;
	};
}

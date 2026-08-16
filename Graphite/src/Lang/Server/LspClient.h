#pragma once

#include "Core/Platform/ChildProcess.h"
#include "Lang/Api/LanguageService.h"
#include "Lang/Server/JsonRpc.h"
#include "Lang/Server/ServerRegistry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
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
	std::size_t ToUtf16Column(std::string_view line, std::size_t byteColumn);
	std::size_t FromUtf16Column(std::string_view line, std::size_t utf16Column);

	// The columns these two return are still the protocol's UTF-16 units: a pure function has no
	// text to convert against. LspClient converts them to bytes before an answer reaches Drain.
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

		// UI thread only. Moves everything answered since the last call into `answers` and
		// reports where the server is.
		void Drain(std::vector<Answer>& answers, ServerStatus& status);

		void Cancel(RequestId id);

	private:
		// What a reply id was asked for. The protocol does not echo the method back, so the
		// question has to be remembered against its id to know how to read the answer.
		enum class Question : std::uint8_t
		{
			Definition,
			References,
			Symbols
		};

		// The server's copy of an open document, as far as this side knows it. Text is kept
		// because a position leaving here has to be measured against the line it points into,
		// and the reply's columns have to be measured back.
		struct Document
		{
			int Version = 0;
			std::string Text;
		};

		// What was asked under an id. The document is remembered alongside the question because
		// a documentSymbol answer names no file of its own, and its columns still have to be
		// measured back against the text they came from.
		struct Asked
		{
			Question Kind = Question::Definition;
			std::string Uri;
		};

		// Held through a shared_ptr so a reader thread still winding down after this object is
		// gone has somewhere valid to write, exactly as SearchService's jobs do.
		struct Shared
		{
			// Guards what Drain collects, and only that. Never held while anything writes to the
			// server, so Drain cannot end up waiting behind a pipe write.
			std::mutex StateMutex;
			std::vector<Answer> Answers;
			ServerStatus Status;

			// Guards the protocol: id allocation, the outstanding table, the documents, the
			// queue of messages the handshake has not released yet, and the write end of the
			// pipe — which must be written a whole framed message at a time or two threads
			// interleave halves of two messages into one unparsable stream.
			std::mutex ProtocolMutex;
			JsonRpc Rpc;
			std::shared_ptr<Core::ChildProcess> Process;
			std::unordered_map<RequestId, Asked> Pending;
			std::unordered_map<std::string, Document> Documents;
			std::vector<std::string> Queued;
			RequestId InitializeId = 0;
			bool Initialized = false;

			std::string ProtocolLanguageId;

			// Decided once at construction: a root with no compile database is a state the user
			// has to fix, not a failure, and it outlives every reply that would otherwise report
			// the server simply Ready.
			bool NoCompileDatabase = false;

			// Set before the child is killed, so the reader can tell a shutdown it was asked for
			// from a server that died on its own.
			std::atomic<bool> Stopping{false};
		};

		// Numbers a question, remembers what was asked so its reply can be read, and sends it.
		// The caller already holds ProtocolMutex: a position's params can only be built while the
		// document text they are measured against is held still.
		RequestId Ask(std::string_view method, const nlohmann::json& params, Question question,
					  const std::string& uri);

		// Everything below runs on the reader thread and takes Shared explicitly, because by the
		// time some of it runs there may be no LspClient left to reach through.
		static void RunReader(std::shared_ptr<Shared> shared);
		static void HandleBody(Shared& shared, const std::string& body);

		// Both are called with ProtocolMutex held.
		static void AnswerServerRequest(Shared& shared, const IncomingMessage& request);
		static void CompleteHandshake(Shared& shared, const nlohmann::json& result);

		static void ApplyProgress(Shared& shared, const nlohmann::json& params);

		// Both write a whole framed message and both need ProtocolMutex held by the caller —
		// that is what keeps two threads from interleaving halves of two messages into one
		// unparsable stream. SendLocked holds a message back until the handshake has finished;
		// SendNowLocked is for the handshake itself and for answers a server is waiting on.
		static void SendLocked(Shared& shared, const std::string& bytes);
		static void SendNowLocked(Shared& shared, const std::string& bytes);

		// The UTF-16 column to send for a byte column, measured against the open document's line.
		// ProtocolMutex held by the caller.
		static std::size_t ColumnFor(Shared& shared, const std::string& uri,
									 const FilePosition& where);

		// Turns the protocol's UTF-16 columns into the byte columns this editor counts in,
		// against the open document's text or, when the answer names a file no tab holds, the
		// file on disk. Takes ProtocolMutex itself, so the caller must not hold it.
		static void ToByteColumns(Shared& shared, Answer& answer, const std::string& askedUri);

		std::shared_ptr<Shared> m_Shared = std::make_shared<Shared>();

		// Declared last so it is destroyed last; the destructor joins it explicitly first.
		std::thread m_Reader;
	};
}

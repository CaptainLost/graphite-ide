#pragma once

#include "Lang/Api/LanguageId.h"
#include "Lang/Api/LanguageService.h"
#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerRegistry.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	// How long to wait before trying again after the Nth restart of a server that keeps dying —
	// zero for the first attempt, growing by two seconds each time after. Free and pure so the
	// backoff itself is checkable without a process: Poll only ever compares a clock against a
	// point computed from this, it never sleeps, because Poll runs on the UI thread once a frame
	// and a wait implemented by blocking would freeze the application for the length of it.
	std::chrono::steady_clock::duration RestartBackoff(std::size_t restarts);

	// Owns every language server the application runs: at most one `LspClient` per
	// (language, project root), started the first time a document in that language and root is
	// seen, restarted a bounded number of times if its process dies, and shut down when this
	// object is destroyed.
	class LanguageServices
	{
	public:
		// `fallbackRoot` is handed to `RootFor` for a file under no recognised project layout —
		// ordinarily the workspace root.
		explicit LanguageServices(std::filesystem::path fallbackRoot);

		~LanguageServices();

		LanguageServices(const LanguageServices&) = delete;
		LanguageServices& operator=(const LanguageServices&) = delete;

		// Starts a server for this file's language if one is configured and none runs for its
		// root yet. Safe to call every time a document opens — and every frame a document stays
		// active, which is how the editor actually calls it: resolving the config and the root is
		// cheap, and an entry already present makes the rest of the call a single map lookup.
		void EnsureStarted(const std::filesystem::path& path, LanguageId language);

		// Overrides the binary `FindServerBinary` would otherwise search for, for every root of
		// this language started from here on. Does not touch a server already running — call
		// before the first `EnsureStarted` for it to take effect.
		void SetConfiguredBinary(LanguageId language, std::filesystem::path path);

		// The status of some server for this language, or a default (State == NotConfigured) if
		// none has ever been started. When more than one root is running the same language, this
		// reports whichever the map happens to order first — every caller in this codebase works
		// with one project root at a time, so that ambiguity has never mattered in practice.
		ServerStatus StatusFor(LanguageId language) const;

		// UI thread, once a frame: collects each running client's replies and status, notices a
		// process that has died, and restarts it once its backoff has elapsed. Never blocks. This
		// is the only place any `LspClient::Drain` is called — see `TakeAnswers`.
		void Poll();

		// Unique across every server this owns, unlike `LspClient::RequestId`, which is numbered
		// per client and restarts at 1 for each one — two different servers would otherwise hand
		// out the same number, and a consumer matching on that alone would attribute one server's
		// answer to the other's question. A consumer holds one of these and matches `Answer::For`
		// against it.
		enum class Ticket : std::uint64_t
		{
		};

		// Each returns a ticket immediately; the answer arrives through `TakeAnswers`. A ticket
		// for a language with no configured server, or for a root whose server never got a
		// process, is answered `Failed` on the very next `TakeAnswers` — there is no such thing as
		// a ticket nobody ever hears back about.
		Ticket AskDefinition(const std::filesystem::path& path, LanguageId language,
							const FilePosition& where);
		Ticket AskReferences(const std::filesystem::path& path, LanguageId language,
							const FilePosition& where);
		Ticket AskSymbols(const std::filesystem::path& path, LanguageId language);

		struct Answer
		{
			Ticket For{};
			std::vector<Location> Locations;
			std::vector<Declaration> Declarations;
			bool Failed = false;
		};

		// Moves everything answered since the last call into `answers`. UI thread only, and the
		// only place answers leave this class.
		//
		// `LspClient::Drain` *consumes* what it returns — a second caller draining the same
		// client would see nothing, because the first caller already took it. `Poll` has to call
		// `Drain` on every client it owns to notice a dead process, so it is also the only place
		// that may collect answers; if a second caller drained a client directly, answers would
		// vanish at random depending on which caller ran first in a frame, which is the worst
		// failure shape there is — indistinguishable from a server that occasionally forgets to
		// answer. `TakeAnswers` is therefore the only way anything outside this class ever sees an
		// answer, and there is deliberately no accessor to a raw `LspClient` for a second drain
		// loop to form around.
		void TakeAnswers(std::vector<Answer>& answers);

		void Cancel(Ticket ticket);

		// Forwarded to the matching client and remembered here, so a restart can tell it again —
		// a fresh `LspClient` knows nothing about what was open, and re-sending is the only way
		// its answers are ever about the file the caller is looking at. A no-op when no server is
		// configured or running for this language and root.
		void OpenDocument(const std::filesystem::path& path, LanguageId language,
						  const std::string& text);
		void ChangeDocument(const std::filesystem::path& path, LanguageId language,
							const std::string& text);

		// Matters less than the other three, and deliberately so: `ChangeDocument` already keeps
		// the server's copy in step with the buffer, so no answer depends on this. What a
		// didSave tells the server is that the file on disk now matches what it has been sent,
		// which is what lets it re-index from disk instead of from its own in-memory copy.
		// Without this the counts are still right; the server just works harder for them.
		//
		// Nothing is remembered for a restart, unlike the other three: a fresh client is told
		// what is open and what it contains, and whether a save happened before it existed says
		// nothing it can act on.
		void SaveDocument(const std::filesystem::path& path, LanguageId language);

		void CloseDocument(const std::filesystem::path& path, LanguageId language);

	private:
		// Identifies one server: a language and the project root it was started for. Two files of
		// the same language under different roots get different servers, which is the whole
		// reason the root is part of the key rather than an implementation detail of the client.
		struct Key
		{
			LanguageId Language = LanguageId::PlainText;
			std::filesystem::path Root;

			friend bool operator<(const Key& lhs, const Key& rhs)
			{
				if (lhs.Language != rhs.Language)
				{
					return lhs.Language < rhs.Language;
				}

				return lhs.Root < rhs.Root;
			}
		};

		// Everything owned about one server, whether or not it ever got a process.
		struct ManagedServer
		{
			const ServerConfig* Config = nullptr; // points into ServerConfigFor's static storage
			std::filesystem::path Root;
			std::filesystem::path Binary; // resolved once; a restart reuses it rather than
										  // searching again
			std::unique_ptr<LspClient> Client; // null when the binary was never found
			ServerStatus Status;
			std::size_t Restarts = 0;

			// Set the moment a Poll first sees this server Failed, cleared once it restarts (or
			// once it settles for good at three). Comparing against `RestartAt` rather than
			// sleeping is what keeps a frame that finds a dead server cheap.
			bool RestartPending = false;
			std::chrono::steady_clock::time_point RestartAt{};

			// The text last sent for every document this server was told is open, so a restarted
			// client can be told the same thing again.
			std::map<std::filesystem::path, std::string> OpenDocuments;

			// What ticket a still-outstanding `RequestId` of *this* client belongs to. RequestId
			// is only meaningful together with the client that issued it — this is the map that
			// makes that pairing explicit, and it is why the ticket, not the RequestId, is what a
			// caller outside this class ever holds.
			std::unordered_map<RequestId, Ticket> Pending;
		};

		// Resolves the config and the root for (path, language); null when the language has no
		// configured server. Shared by every method that needs to find a server's entry rather
		// than start one — including EnsureStarted, which is called every frame a document stays
		// active, so this is on the hot path. Not const: it consults and fills m_RootCache.
		const ServerConfig* Resolve(const std::filesystem::path& path, LanguageId language,
									Key& key);

		std::filesystem::path ConfiguredBinaryFor(LanguageId language) const;

		// Resolves the binary and either constructs the client or records why it could not.
		void StartServer(ManagedServer& server, const Key& key);

		// Tears down a dead client and stands up a fresh one in its place. Never revives the old
		// object: `LspClient::ReaderStopped`/`WriterStopped` are one-way, set once its threads
		// have ended, so an `LspClient` whose process died is finished for good — the only way
		// back is a brand new instance, which is what this does.
		void RestartServer(const Key& key, ManagedServer& server);

		Ticket IssueTicket();

		// Records an immediately-Failed answer for a ticket that will never reach a client —
		// no server configured, or no process ever started for one that was.
		void Fail(Ticket ticket);

		// Shared by AskDefinition/AskReferences/AskSymbols: resolves (path, language) to a
		// server, and either forwards through `ask` and remembers the resulting RequestId against
		// a fresh ticket, or fails the ticket immediately when there is no live client to ask.
		Ticket AskVia(const std::filesystem::path& path, LanguageId language,
					 const std::function<RequestId(LspClient&)>& ask);

		std::filesystem::path m_FallbackRoot;
		std::map<LanguageId, std::filesystem::path> m_ConfiguredBinaries;
		std::map<Key, ManagedServer> m_Servers;

		// A document's directory resolves to the same root every time RootFor would walk it, for
		// as long as this object lives — a project's layout does not move mid-session. Keyed on
		// (language, parent directory) rather than the full file path, so every document in the
		// same directory shares one entry. Without this, EnsureStarted — called every frame a
		// document is active, per its own contract — would call RootFor, and so
		// std::filesystem::exists, once per marker per directory level, sixty times a second, for
		// a server that is already running and needs nothing. If a root ever did move during a
		// session, the stale entry would point a server at where the project used to live; that
		// is wrong but not silently so — restarting the application re-walks from scratch — and
		// is a better trade than stat-ing the filesystem on every frame forever.
		std::map<std::pair<LanguageId, std::filesystem::path>, std::filesystem::path> m_RootCache;

		std::uint64_t m_NextTicket = 1;
		std::vector<Answer> m_Answers;
	};
}

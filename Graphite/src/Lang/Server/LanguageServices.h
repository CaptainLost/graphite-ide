#pragma once

#include "Lang/Api/LanguageId.h"
#include "Lang/Api/LanguageService.h"
#include "Lang/Server/LspClient.h"
#include "Lang/Server/ServerRegistry.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

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
		// process that has died, and restarts it once its backoff has elapsed. Never blocks.
		void Poll();

		// The client managing this file's language and root, or null when none is configured,
		// none has been started yet, or the one that was started never got a process (NotFound,
		// or Failed after exhausting its restarts).
		LspClient* ClientFor(const std::filesystem::path& path, LanguageId language);

		// Forwarded to the matching client and remembered here, so a restart can tell it again —
		// a fresh `LspClient` knows nothing about what was open, and re-sending is the only way
		// its answers are ever about the file the caller is looking at. A no-op when no server is
		// configured or running for this language and root.
		void OpenDocument(const std::filesystem::path& path, LanguageId language,
						  const std::string& text);
		void ChangeDocument(const std::filesystem::path& path, LanguageId language,
							const std::string& text);
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
		};

		// Resolves the config and the root for (path, language); null when the language has no
		// configured server. Shared by every method that needs to find a server's entry rather
		// than start one.
		const ServerConfig* Resolve(const std::filesystem::path& path, LanguageId language,
									Key& key) const;

		std::filesystem::path ConfiguredBinaryFor(LanguageId language) const;

		// Resolves the binary and either constructs the client or records why it could not.
		void StartServer(ManagedServer& server, const Key& key);

		// Tears down a dead client and stands up a fresh one in its place. Never revives the old
		// object: `LspClient::ReaderStopped`/`WriterStopped` are one-way, set once its threads
		// have ended, so an `LspClient` whose process died is finished for good — the only way
		// back is a brand new instance, which is what this does.
		void RestartServer(const Key& key, ManagedServer& server);

		std::filesystem::path m_FallbackRoot;
		std::map<LanguageId, std::filesystem::path> m_ConfiguredBinaries;
		std::map<Key, ManagedServer> m_Servers;
	};
}

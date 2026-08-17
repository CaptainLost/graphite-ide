#include "Lang/Server/LanguageServices.h"

#include "Core/Fs/FileSystem.h"
#include "Lang/Server/ServerDiscovery.h"

#include <utility>

namespace Graphite {

	std::chrono::steady_clock::duration RestartBackoff(std::size_t restarts)
	{
		return std::chrono::seconds(static_cast<long long>(restarts) * 2);
	}

	LanguageServices::LanguageServices(std::filesystem::path fallbackRoot)
		: m_FallbackRoot(std::move(fallbackRoot))
	{
	}

	LanguageServices::~LanguageServices()
	{
		// Asked to leave politely, but not waited on: a server wedged on something has no way to
		// tell us apart from one that is merely slow, and the UI thread closing down cannot sit
		// here to find out which. Each client's own destructor is what actually bounds this —
		// it terminates the process and joins its threads unconditionally, whether or not either
		// message was ever read.
		for (auto& [key, server] : m_Servers)
		{
			if (server.Client)
			{
				server.Client->RequestShutdown();
			}
		}
	}

	const ServerConfig* LanguageServices::Resolve(const std::filesystem::path& path,
												   LanguageId language, Key& key)
	{
		const ServerConfig* config = ServerConfigFor(language);

		if (config == nullptr)
		{
			return nullptr;
		}

		key.Language = language;

		// The directory, not the full path: every document in it resolves to the same root, and
		// most callers here are one call per open document, not one per unique file name.
		const auto cacheKey = std::make_pair(language, path.parent_path());
		const auto cached = m_RootCache.find(cacheKey);

		if (cached != m_RootCache.end())
		{
			key.Root = cached->second;
			return config;
		}

		key.Root = RootFor(*config, path, m_FallbackRoot);
		m_RootCache.emplace(cacheKey, key.Root);

		return config;
	}

	std::filesystem::path LanguageServices::ConfiguredBinaryFor(LanguageId language) const
	{
		const auto found = m_ConfiguredBinaries.find(language);
		return found == m_ConfiguredBinaries.end() ? std::filesystem::path{} : found->second;
	}

	void LanguageServices::SetConfiguredBinary(LanguageId language, std::filesystem::path path)
	{
		m_ConfiguredBinaries[language] = std::move(path);
	}

	void LanguageServices::StartServer(ManagedServer& server, const Key& key)
	{
		const Core::Result<std::filesystem::path> binary =
			FindServerBinary(server.Config->Command, ConfiguredBinaryFor(key.Language));

		server.Status.Name = std::string(server.Config->Command);
		server.Status.Restarts = server.Restarts;

		if (!binary)
		{
			server.Status.State = ServerState::NotFound;
			server.Status.Detail = binary.GetError().Message;
			return;
		}

		server.Binary = binary.GetValue();
		server.Client = std::make_unique<LspClient>(*server.Config, server.Binary, key.Root);

		// The client's own Drain reports the real state — Starting, then Indexing/Ready/Failed —
		// from the next Poll onward. This is only what a caller sees before that first Poll.
		server.Status.State = ServerState::Starting;
		server.Status.Detail = Core::ToUtf8(server.Binary);
	}

	void LanguageServices::EnsureStarted(const std::filesystem::path& path, LanguageId language)
	{
		Key key;
		const ServerConfig* config = Resolve(path, language, key);

		if (config == nullptr)
		{
			// No server configured for this language: nothing to start, and no entry to create.
			// StatusFor answers NotConfigured for a language with no entry, so there is nothing
			// else to record here.
			return;
		}

		if (m_Servers.contains(key))
		{
			// Already running, or already failed to start, for this exact (language, root) pair.
			// EnsureStarted is called every time a document in this language becomes active, so
			// this is the check that keeps the common case a single lookup.
			return;
		}

		ManagedServer server;
		server.Config = config;
		server.Root = key.Root;

		StartServer(server, key);

		m_Servers.emplace(key, std::move(server));
	}

	ServerStatus LanguageServices::StatusFor(LanguageId language) const
	{
		for (const auto& [key, server] : m_Servers)
		{
			if (key.Language == language)
			{
				return server.Status;
			}
		}

		// No entry at all: never configured, or never started. Both read the same way to a
		// caller — ServerStatus{} defaults to State == NotConfigured.
		return {};
	}

	LanguageServices::Ticket LanguageServices::IssueTicket()
	{
		return static_cast<Ticket>(m_NextTicket++);
	}

	void LanguageServices::Fail(Ticket ticket)
	{
		Answer answer;
		answer.For = ticket;
		answer.Failed = true;

		m_Answers.push_back(std::move(answer));
	}

	LanguageServices::Ticket LanguageServices::AskVia(
		const std::filesystem::path& path, LanguageId language,
		const std::function<RequestId(LspClient&)>& ask)
	{
		const Ticket ticket = IssueTicket();

		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			// Nothing configured for this language — there is no client that will ever answer,
			// so the ticket is failed on the spot rather than left to hang forever.
			Fail(ticket);
			return ticket;
		}

		const auto found = m_Servers.find(key);

		if (found == m_Servers.end() || !found->second.Client)
		{
			// Not started yet, or started and never got a process (NotFound, or Failed after
			// exhausting its restarts) — same reasoning as above.
			Fail(ticket);
			return ticket;
		}

		ManagedServer& server = found->second;
		const RequestId requestId = ask(*server.Client);

		server.Pending[requestId] = ticket;

		return ticket;
	}

	LanguageServices::Ticket LanguageServices::AskDefinition(const std::filesystem::path& path,
															  LanguageId language,
															  const FilePosition& where)
	{
		return AskVia(path, language,
					 [&where](LspClient& client) { return client.AskDefinition(where); });
	}

	LanguageServices::Ticket LanguageServices::AskReferences(const std::filesystem::path& path,
															   LanguageId language,
															   const FilePosition& where,
															   ReferenceScope scope)
	{
		return AskVia(path, language, [&where, scope](LspClient& client) {
			return client.AskReferences(where, scope);
		});
	}

	LanguageServices::Ticket LanguageServices::AskSymbols(const std::filesystem::path& path,
														   LanguageId language)
	{
		return AskVia(path, language,
					 [&path](LspClient& client) { return client.AskSymbols(path); });
	}

	void LanguageServices::TakeAnswers(std::vector<Answer>& answers)
	{
		answers.insert(answers.end(), std::make_move_iterator(m_Answers.begin()),
					   std::make_move_iterator(m_Answers.end()));

		m_Answers.clear();
	}

	void LanguageServices::Cancel(Ticket ticket)
	{
		for (auto& [key, server] : m_Servers)
		{
			for (auto pending = server.Pending.begin(); pending != server.Pending.end(); ++pending)
			{
				if (pending->second != ticket)
				{
					continue;
				}

				if (server.Client)
				{
					server.Client->Cancel(pending->first);
				}

				server.Pending.erase(pending);
				return;
			}
		}
	}

	void LanguageServices::OpenDocument(const std::filesystem::path& path, LanguageId language,
										std::string text)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return;
		}

		const auto found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return;
		}

		ManagedServer& server = found->second;

		// Remembered before forwarding, not instead of it: a restart needs the text on hand with
		// nowhere else to read it from, since the client that used to hold it is the one that
		// just died.
		std::string& kept = server.OpenDocuments[path];
		kept = std::move(text);

		// The kept copy, not a second one: `text` has been moved from, and the client only reads
		// what it is given long enough to escape it into JSON.
		if (server.Client)
		{
			server.Client->OpenDocument(path, kept);
		}
	}

	void LanguageServices::ChangeDocument(const std::filesystem::path& path, LanguageId language,
										  std::string text)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return;
		}

		const auto found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return;
		}

		ManagedServer& server = found->second;

		const auto document = server.OpenDocuments.find(path);

		if (document == server.OpenDocuments.end())
		{
			// Never opened through here — nothing to keep in step with, and forwarding a change
			// for a document the server was never told about would be a change to nothing.
			return;
		}

		document->second = std::move(text);

		if (server.Client)
		{
			server.Client->ChangeDocument(path, document->second);
		}
	}

	void LanguageServices::SaveDocument(const std::filesystem::path& path, LanguageId language)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return;
		}

		const auto found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return;
		}

		ManagedServer& server = found->second;

		// The same guard ChangeDocument uses: a didSave for a document the server was never told
		// about is a notification about nothing, and clangd is entitled to complain about one.
		if (server.OpenDocuments.find(path) == server.OpenDocuments.end())
		{
			return;
		}

		if (server.Client)
		{
			server.Client->SaveDocument(path);
		}
	}

	void LanguageServices::CloseDocument(const std::filesystem::path& path, LanguageId language)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return;
		}

		const auto found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return;
		}

		ManagedServer& server = found->second;

		if (server.OpenDocuments.erase(path) == 0)
		{
			return;
		}

		if (server.Client)
		{
			server.Client->CloseDocument(path);
		}
	}

	void LanguageServices::RestartServer(const Key& key, ManagedServer& server)
	{
		// A dead LspClient's reader and writer threads have already stopped for good — that flag
		// never resets — so the only way back is a new object, never a Reset() on this one.
		// Destroying the old client here is safe: its threads are already joined, so this runs
		// the same teardown a normal destruction would, just without a live process to terminate.
		server.Client.reset();
		server.Client = std::make_unique<LspClient>(*server.Config, server.Binary, key.Root);

		// A fresh server knows nothing about what is open. Every document remembered for this
		// server is sent again, in no particular order — didOpen does not depend on prior state.
		for (const auto& [path, text] : server.OpenDocuments)
		{
			server.Client->OpenDocument(path, text);
		}

		server.Restarts += 1;
		server.RestartPending = false;

		server.Status.State = ServerState::Starting;
		server.Status.Detail = Core::ToUtf8(server.Binary);
		server.Status.Restarts = server.Restarts;
	}

	void LanguageServices::Poll()
	{
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		for (auto& [key, server] : m_Servers)
		{
			if (!server.Client)
			{
				// Never got a process (NotFound), or settled at Failed after exhausting its
				// restarts below — either way there is nothing running to drain.
				continue;
			}

			std::vector<LspClient::Answer> answers;
			ServerStatus status;
			server.Client->Drain(answers, status);

			// This is the single place any LspClient::Answer is turned into a ticketed Answer —
			// see TakeAnswers for why there must be exactly one. A RequestId with no entry in
			// Pending was already cancelled (Cancel erases it there without waiting for the
			// server's reply), so it is dropped rather than delivered to nobody.
			for (LspClient::Answer& answer : answers)
			{
				const auto pending = server.Pending.find(answer.Id);

				if (pending == server.Pending.end())
				{
					continue;
				}

				Answer delivered;
				delivered.For = pending->second;
				delivered.Locations = std::move(answer.Locations);
				delivered.Declarations = std::move(answer.Declarations);
				delivered.Failed = answer.Failed;

				m_Answers.push_back(std::move(delivered));
				server.Pending.erase(pending);
			}

			status.Restarts = server.Restarts;
			server.Status = status;

			if (status.State != ServerState::Failed)
			{
				server.RestartPending = false;
				continue;
			}

			// A dead client's reader answers everything it still held outstanding as Failed
			// before it reports Failed at all (LspClient's own invariant), so by this point
			// Pending should already be empty. Cleared here as well in case a ticket was issued
			// in the same frame the client died, after the reader's own sweep but before this
			// Drain — belt and braces so a ticket is never silently forgotten.
			for (auto& entry : server.Pending)
			{
				Fail(entry.second);
			}
			server.Pending.clear();

			if (server.Restarts >= 3)
			{
				// Settled: three attempts have already failed, and Status.State == Failed is
				// exactly what a caller reading StatusFor should see from here on.
				continue;
			}

			if (!server.RestartPending)
			{
				// Just noticed the death this frame. Scheduling the retry rather than attempting
				// it immediately keeps this path uniform whether the backoff is zero (the first
				// restart) or several seconds (a later one) — either way the actual restart
				// happens once the clock says so, never inside this branch.
				server.RestartPending = true;
				server.RestartAt = now + RestartBackoff(server.Restarts);
			}

			if (now < server.RestartAt)
			{
				continue;
			}

			RestartServer(key, server);
		}
	}
}

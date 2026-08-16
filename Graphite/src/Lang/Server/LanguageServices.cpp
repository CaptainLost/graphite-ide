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
												   LanguageId language, Key& key) const
	{
		const ServerConfig* config = ServerConfigFor(language);

		if (config == nullptr)
		{
			return nullptr;
		}

		key.Language = language;
		key.Root = RootFor(*config, path, m_FallbackRoot);

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

	LspClient* LanguageServices::ClientFor(const std::filesystem::path& path, LanguageId language)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return nullptr;
		}

		const auto found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return nullptr;
		}

		return found->second.Client.get();
	}

	void LanguageServices::OpenDocument(const std::filesystem::path& path, LanguageId language,
										const std::string& text)
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
		server.OpenDocuments[path] = text;

		if (server.Client)
		{
			server.Client->OpenDocument(path, text);
		}
	}

	void LanguageServices::ChangeDocument(const std::filesystem::path& path, LanguageId language,
										  const std::string& text)
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

		document->second = text;

		if (server.Client)
		{
			server.Client->ChangeDocument(path, text);
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

			// Answers themselves belong to whichever editor-facing code asked the question that
			// produced them — LanguageServices owns the server, not the conversation with it —
			// so nothing here inspects them beyond having drained them out of the client.
			status.Restarts = server.Restarts;
			server.Status = status;

			if (status.State != ServerState::Failed)
			{
				server.RestartPending = false;
				continue;
			}

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

#include "Lang/Server/LanguageServices.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/ServerDiscovery.h"
#include "Search/FileEnumerator.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		// Bytes as they sit on disk, which is what a server wants: it re-reads the file itself for
		// its index, and this copy exists only to make it load the compilation database. A third
		// copy of this three-line helper rather than a shared one, matching what PreviewContent
		// and SearchService already do - worth extracting when something needs a fourth.
		std::string ReadAll(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return {};
			}

			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
	}

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

			APP_WARN("No {} for {}: {}", server.Config->Command,
					 LanguageDisplayName(key.Language), binary.GetError().Message);
			return;
		}

		server.Binary = binary.GetValue();

		// Logged at the moment a process is about to exist, with everything needed to explain a
		// later answer: which language it serves, which binary is running, and the root it was
		// given - a server rooted somewhere unexpected answers about the wrong project, and this
		// line is where that becomes visible instead of mysterious.
		APP_INFO("Starting {} for {} at {}", server.Config->Command,
				 LanguageDisplayName(key.Language), Core::ToUtf8(key.Root));

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

		if (Permits(key.Root))
		{
			StartServer(server, key);
		}
		else
		{
			MarkRestricted(server);
		}

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

	std::optional<ServerStatus> LanguageServices::OverallStatus() const
	{
		const auto rank = [](ServerState state) -> int
		{
			switch (state)
			{
				case ServerState::Indexing:
				case ServerState::Starting:
					return 3; // something is happening and will stop happening
				case ServerState::Failed:
				case ServerState::NotFound:
				case ServerState::NoCompileDatabase:
				case ServerState::Restricted:
					return 2; // something is wrong and will not fix itself
				case ServerState::Ready:
					return 1; // up and answering, which is worth saying when nothing else is
				case ServerState::NotConfigured:
					return 0; // no server here at all - the one case with nothing to report
			}

			return 0;
		};

		const ServerStatus* best = nullptr;

		for (const auto& [key, server] : m_Servers)
		{
			if (rank(server.Status.State) == 0)
			{
				continue;
			}

			if (best == nullptr || rank(server.Status.State) > rank(best->State))
			{
				best = &server.Status;
			}
		}

		return best != nullptr ? std::optional<ServerStatus>{*best} : std::nullopt;
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
															   const FilePosition& where)
	{
		return AskVia(path, language,
					 [&where](LspClient& client) { return client.AskReferences(where); });
	}

	LanguageServices::Ticket LanguageServices::AskSemanticTokens(const std::filesystem::path& path,
																  LanguageId language)
	{
		return AskVia(path, language,
					 [&path](LspClient& client) { return client.AskSemanticTokens(path); });
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

	void LanguageServices::TakeDiagnostics(DiagnosticsUpdate& update)
	{
		update.Published.insert(update.Published.end(),
								std::make_move_iterator(m_Diagnostics.Published.begin()),
								std::make_move_iterator(m_Diagnostics.Published.end()));
		update.Versions.insert(update.Versions.end(),
							   std::make_move_iterator(m_Diagnostics.Versions.begin()),
							   std::make_move_iterator(m_Diagnostics.Versions.end()));
		update.Closed.insert(update.Closed.end(),
							 std::make_move_iterator(m_Diagnostics.Closed.begin()),
							 std::make_move_iterator(m_Diagnostics.Closed.end()));

		m_Diagnostics.Published.clear();
		m_Diagnostics.Versions.clear();
		m_Diagnostics.Closed.clear();
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
		ManagedServer::SentDocument& kept = server.OpenDocuments[path];
		kept.Text = std::move(text);

		// The kept copy, not a second one: `text` has been moved from, and the client only reads
		// what it is given long enough to escape it into JSON.
		if (server.Client)
		{
			// LspClient::OpenDocument always sends and always returns a non-default version, so
			// nothing here needs to guard against a default one - `if (server.Client)` above is
			// what suppresses the note on a machine with no live process to send it to.
			kept.Version = server.Client->OpenDocument(path, kept.Text);
			m_Diagnostics.Versions.push_back(DocumentVersionNote{path, kept.Version});
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

		document->second.Text = std::move(text);

		if (server.Client)
		{
			document->second.Version = server.Client->ChangeDocument(path, document->second.Text);

			if (document->second.Version != DocumentVersion{})
			{
				m_Diagnostics.Versions.push_back(
					DocumentVersionNote{path, document->second.Version});
			}
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
			// A note only when a live client was actually told - a document never sent (no
			// binary found) has no version note either, so the model has no entry for it to
			// drop in the first place.
			m_Diagnostics.Closed.push_back(path);
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

		// A fresh server knows nothing about what is open, and its numbering restarts at 1 - the
		// old process's diagnostics, measured against the old numbering, are no longer about
		// anything a new comparison can trust. Every document remembered for this server is sent
		// again, in no particular order - didOpen does not depend on prior state - and each is
		// reported both closed and reopened, so a reader holding a stale entry for it drops that
		// entry before it ever compares the wrong process's version against the new one.
		ReplayOpenDocuments(server);

		server.Restarts += 1;
		server.RestartPending = false;

		server.Status.State = ServerState::Starting;
		server.Status.Detail = Core::ToUtf8(server.Binary);
		server.Status.Restarts = server.Restarts;
	}

	void LanguageServices::SetRootPermission(RootPermission permission)
	{
		m_RootPermission = std::move(permission);
	}

	bool LanguageServices::Permits(const std::filesystem::path& root) const
	{
		return !m_RootPermission || m_RootPermission(root);
	}

	void LanguageServices::MarkRestricted(ManagedServer& server)
	{
		server.Status.Name = std::string(server.Config->Command);
		server.Status.State = ServerState::Restricted;
		server.Status.Detail = Core::ToUtf8(server.Root);
		server.Status.Percent = -1;
		server.Status.Restarts = 0;
	}

	void LanguageServices::Restrict(ManagedServer& server)
	{
		if (server.Client)
		{
			server.Client->RequestShutdown();

			// Whatever it published is about a process that is going away. A closed note is
			// what makes a reader drop it, the same as a restart's.
			for (const auto& [path, document] : server.OpenDocuments)
			{
				m_Diagnostics.Closed.push_back(path);
			}

			// The destructor terminates the process and joins its threads, as it does at exit.
			server.Client.reset();
		}

		for (auto& entry : server.Pending)
		{
			Fail(entry.second);
		}
		server.Pending.clear();

		server.Restarts = 0;
		server.RestartPending = false;

		MarkRestricted(server);
	}

	void LanguageServices::ReplayOpenDocuments(ManagedServer& server)
	{
		for (auto& [path, document] : server.OpenDocuments)
		{
			document.Version = server.Client->OpenDocument(path, document.Text);

			m_Diagnostics.Closed.push_back(path);
			m_Diagnostics.Versions.push_back(DocumentVersionNote{path, document.Version});
		}
	}

	void LanguageServices::Revalidate()
	{
		for (auto& [key, server] : m_Servers)
		{
			const bool permitted = Permits(key.Root);
			const bool restricted = server.Status.State == ServerState::Restricted;

			if (permitted && restricted)
			{
				// A fresh start, not a restart: the documents it holds were never sent to any
				// process, so this is their first didOpen, and no restart is counted.
				StartServer(server, key);

				if (server.Client)
				{
					ReplayOpenDocuments(server);
				}
			}
			else if (!permitted && !restricted)
			{
				Restrict(server);
			}
		}
	}

	void LanguageServices::WarmUp(std::filesystem::path root, bool respectGitIgnore)
	{
		// A walk still running is about the folder that was open a moment ago, and nothing it
		// finds there is wanted now.
		m_WarmToken.Cancel();
		m_WarmToken = Core::CancellationToken{};

		{
			const std::lock_guard<std::mutex> guard(m_Warm->Mutex);
			m_Warm->Found.clear();
		}

		// After the cancel, so a walk already running for this root stops too.
		if (!Permits(root))
		{
			return;
		}

		m_Jobs.Submit([warm = m_Warm, token = m_WarmToken, root = std::move(root),
					   respectGitIgnore]
		{
			const std::span<const LanguageId> wanted = LanguagesWithServers();

			std::vector<LanguageId> seen;

			EnumerationInput enumeration;
			enumeration.Root = root;

			// Opened on this thread rather than shared with the workspace's: libgit2 handles are
			// not safe to use from two threads, which is the same reason SearchService opens its
			// own.
			std::optional<Core::Repository> repository;

			if (respectGitIgnore)
			{
				if (Core::Result<Core::Repository> opened = Core::Repository::Open(root))
				{
					repository.emplace(std::move(opened.GetValue()));

					enumeration.Ignored = [&repository](const std::filesystem::path& path, bool)
					{ return repository->IsIgnored(path); };
				}
			}

			EnumerateFiles(enumeration, token, [&](const std::filesystem::path& file)
			{
				if (seen.size() == wanted.size())
				{
					// Everything worth finding has been found. EnumerateFiles checks the token
					// between entries, so cancelling here stops the walk rather than merely
					// skipping the rest of it — which on a large tree is the whole point.
					token.Cancel();
					return;
				}

				const LanguageId language = LanguageOfPath(file);

				if (ServerConfigFor(language) == nullptr ||
					std::find(seen.begin(), seen.end(), language) != seen.end())
				{
					return;
				}

				seen.push_back(language);

				// Read here, on the walk's thread. Poll needs it to prime the server the moment
				// it starts, and a file of unknown size read inside a frame is exactly what this
				// class does not do.
				std::string text = ReadAll(file);

				{
					const std::lock_guard<std::mutex> guard(warm->Mutex);
					warm->Found.push_back(WarmFinding{language, file, std::move(text)});
				}

				Core::Redraw::Request();
			});
		});
	}

	void LanguageServices::Poll()
	{
		// What the warm-up walk found, turned into servers here rather than on the worker: every
		// other path into EnsureStarted is the UI thread, and m_Servers has no lock because that
		// has always been true.
		std::vector<WarmFinding> warm;

		{
			const std::lock_guard<std::mutex> guard(m_Warm->Mutex);
			warm.swap(m_Warm->Found);
		}

		for (WarmFinding& finding : warm)
		{
			EnsureStarted(finding.Path, finding.Language);

			// Opened and closed straight away, to make the server load its compilation database.
			// This is a workaround and worth naming as one.
			//
			// clangd loads that database lazily - on the first document it is asked about - and
			// loading it is what starts the background index. Verified against clangd 22: twelve
			// seconds after initialize with no document open produces no progress of any kind,
			// and the first didOpen produces it immediately. So without this a server starts,
			// reports itself ready, and then sits idle until the user opens a file - which is
			// exactly the work opening the folder was meant to have underway already.
			//
			// One document is enough for all of them: on an empty index cache, a single didOpen
			// takes the reported work from 0/1 to 0/201 within a tenth of a second - the whole
			// compilation database, not just the file sent - and it keeps going after the close.
			//
			// Two better-looking routes were tried and neither works. workspace/symbol, being a
			// question about the whole project rather than a claim about one document, answers
			// instantly with nothing and loads no database. --compile-commands-dir, which reads
			// as "here is the database, load it", changes nothing either: with an empty cache and
			// that flag set, eighteen seconds after initialize produced no indexing and created
			// no index directory at all. Nothing else in LSP or in clangd's flags says "load your
			// project".
			//
			// What this does say is true, which is why it is a workaround rather than a lie:
			// didOpen means the client owns this URI's content, and for those two messages it
			// does - the content sent is what is on disk. It is closed again immediately because
			// nothing is looking at it, the index task it started is a background one that
			// outlives the document, and leaving it open would collide with a real didOpen if
			// the user later opened that same file.
			//
			// If clangd ever loads its database eagerly, this stops having any effect and
			// nothing breaks - the failure mode is a return to indexing on first open, not a
			// wrong answer.
			if (!finding.Text.empty())
			{
				OpenDocument(finding.Path, finding.Language, std::move(finding.Text));
				CloseDocument(finding.Path, finding.Language);
			}
		}

		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		for (auto& [key, server] : m_Servers)
		{
			if (!server.Client)
			{
				// Never got a process (NotFound), or settled at Failed after exhausting its
				// restarts below — either way there is nothing running to drain.
				continue;
			}

			LspClient::Drained drained;
			server.Client->Drain(drained);

			// This is the single place any LspClient::Answer is turned into a ticketed Answer —
			// see TakeAnswers for why there must be exactly one. A RequestId with no entry in
			// Pending was already cancelled (Cancel erases it there without waiting for the
			// server's reply), so it is dropped rather than delivered to nobody.
			for (LspClient::Answer& answer : drained.Answers)
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
				delivered.SemanticTokens = std::move(answer.SemanticTokens);
				delivered.Failed = answer.Failed;

				m_Answers.push_back(std::move(delivered));
				server.Pending.erase(pending);
			}

			// Moved rather than copied - a publication is one server's word on one document, and
			// Poll is the only place any Drain result is seen, same as drained.Answers above.
			m_Diagnostics.Published.insert(m_Diagnostics.Published.end(),
										   std::make_move_iterator(drained.Diagnostics.begin()),
										   std::make_move_iterator(drained.Diagnostics.end()));

			drained.Status.Restarts = server.Restarts;

			// Logged on the change, not on the state: Poll runs every frame, so anything keyed on
			// the state itself would say the same thing sixty times a second. Indexing is the one
			// worth announcing at both ends — while it runs the server answers from a partial
			// index, so counts read low and definitions in files it has not reached yet are
			// missing, and knowing when that stopped being true is the difference between a wrong
			// answer and a wrong answer with a reason.
			if (drained.Status.State != server.Status.State)
			{
				const std::string_view name = LanguageDisplayName(key.Language);

				if (drained.Status.State == ServerState::Indexing)
				{
					APP_INFO("{} started indexing for {}", server.Status.Name, name);
				}
				else if (server.Status.State == ServerState::Indexing)
				{
					APP_INFO("{} finished indexing for {}", server.Status.Name, name);
				}
				else if (drained.Status.State == ServerState::Failed)
				{
					APP_WARN("{} failed for {}: {}", server.Status.Name, name, drained.Status.Detail);
				}
			}

			server.Status = drained.Status;

			if (drained.Status.State != ServerState::Failed)
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

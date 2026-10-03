#include "Lang/Server/LanguageServices.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Text/FileProbe.h"
#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/ServerDiscovery.h"
#include "Search/FileEnumerator.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace Graphite {

	std::chrono::steady_clock::duration RestartBackoff(std::size_t restarts)
	{
		return std::chrono::seconds(static_cast<long long>(restarts) * 2);
	}

	bool MayRestart(std::size_t restarts)
	{
		return restarts < kMaximumRestarts;
	}

	bool RestartBudgetRefilled(std::optional<std::chrono::steady_clock::time_point> upSince,
							   std::chrono::steady_clock::time_point now)
	{
		return upSince.has_value() && now - *upSince >= kStableAfter;
	}

	LanguageServices::LanguageServices(std::filesystem::path fallbackRoot)
		: m_FallbackRoot(std::move(fallbackRoot))
	{
	}

	LanguageServices::~LanguageServices()
	{
		// m_Jobs joins the warm-up walk, which otherwise runs to the end of a tree with no source
		// of any server.
		m_WarmToken.Cancel();

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

		// An open document answers with the key it was opened under, before anything that could
		// have changed since.
		if (const auto open = m_DocumentKeys.find(path); open != m_DocumentKeys.end() && open->second.Language == language)
		{
			key.Root = open->second.Root;
			return config;
		}

		// Per file, so ahead of the per-directory cache: one named header does not decide for
		// every file beside it. A marker of its own still wins - RootFor only returns the
		// fallback when it finds none - and so does the open folder, which comes before the
		// server that named a file (see the server-roots spec).
		if (const auto named = m_NamedBy.find(path); named != m_NamedBy.end() && named->second.Language == language)
		{
			const bool inFolder = Core::IsSameOrWithin(path, m_FallbackRoot);

			key.Root = RootFor(*config, path, inFolder ? m_FallbackRoot : named->second.Root);
			return config;
		}

		// The directory, not the full path: every document in it resolves to the same root, and
		// most callers here are one call per open document, not one per unique file name.
		const auto cacheKey = std::make_pair(language, path.parent_path());
		const auto cached = m_RootCache.find(cacheKey);

		if (cached != m_RootCache.end())
		{
			key.Root = cached->second;
			return config;
		}

		key.Root = RootFor(*config, path, FallbackFor(path));
		m_RootCache.emplace(cacheKey, key.Root);

		return config;
	}

	// Every file gets a root it can be judged by (ADR 0532). One shared empty root for every
	// file with no marker would make one trust answer cover files from folders the user judged
	// differently, and would be a root no trust entry can ever name.
	std::filesystem::path LanguageServices::FallbackFor(const std::filesystem::path& path) const
	{
		// Case-insensitive, as the trust store compares roots: a path from a URI or the command
		// line can spell the open folder in another case and still lie inside it.
		if (Core::IsSameOrWithin(path, m_FallbackRoot))
		{
			return m_FallbackRoot;
		}

		return path.parent_path();
	}

	void LanguageServices::SetFallbackRoot(std::filesystem::path root)
	{
		m_FallbackRoot = std::move(root);
		m_RootCache.clear();
	}

	void LanguageServices::NoteNamed(const std::filesystem::path& named, const std::filesystem::path& by,
									 LanguageId language)
	{
		Key key;

		if (Resolve(by, language, key) != nullptr)
		{
			NoteNamedByKey(named, key);
		}
	}

	void LanguageServices::NoteNamedByKey(const std::filesystem::path& named, const Key& key)
	{
		// An open document already belongs somewhere, and nothing moves it.
		if (!m_DocumentKeys.contains(named))
		{
			m_NamedBy[named] = key;
		}
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

		server.Client = MakeClient(server, key);

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

	ServerStatus LanguageServices::StatusFor(const std::filesystem::path& path, LanguageId language)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return {};
		}

		const auto found = m_Servers.find(key);

		return found != m_Servers.end() ? found->second.Status : ServerStatus{};
	}

	std::optional<ServerStatus> LanguageServices::OverallStatus() const
	{
		const auto rank = [](ServerState state) -> int {
			switch (state)
			{
			case ServerState::Indexing:
			case ServerState::Starting:
				return 4; // something is happening and will stop happening
			case ServerState::Failed:
			case ServerState::NotFound:
			case ServerState::NoCompileDatabase:
				return 3; // something is wrong and will not fix itself
			case ServerState::Ready:
				return 2; // up and answering, which is worth saying when nothing else is
			case ServerState::Restricted:
				// Below Ready: a trusted server that is up is more use to show than an
				// untrusted one somewhere else - one file opened from another folder would
				// otherwise speak for the whole workspace for the rest of the session. Its own
				// tabs still say it, through StatusFor for their path.
				return 1;
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

		if (requestId == 0)
		{
			// The client declined to ask - semantic tokens before the handshake gave it a legend,
			// or from a server that declared none. Parked under id 0 the ticket would never be
			// answered and the next refusal would overwrite it, so it is failed on the spot.
			Fail(ticket);
			return ticket;
		}

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

	LanguageServices::Ticket LanguageServices::AskHover(const std::filesystem::path& path,
														LanguageId language,
														const FilePosition& where)
	{
		return AskVia(path, language,
					  [&where](LspClient& client) { return client.AskHover(where); });
	}

	LanguageServices::Ticket LanguageServices::AskSwitchSourceHeader(
		const std::filesystem::path& path, LanguageId language)
	{
		return AskVia(path, language,
					  [&path](LspClient& client) { return client.AskSwitchSourceHeader(path); });
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

		// Recorded whether or not the server has a process: a NotFound or Restricted entry keeps
		// the document too, and a later Revalidate replays it to that same entry.
		m_DocumentKeys[path] = key;

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

		// Resolve has read the document's key by now, and the document is closed whatever the
		// server makes of it: forgotten on every way out, so the path resolves afresh when it
		// is next opened.
		m_DocumentKeys.erase(path);

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

	bool LanguageServices::Holds(const std::filesystem::path& path, LanguageId language)
	{
		Key key;

		if (Resolve(path, language, key) == nullptr)
		{
			return false;
		}

		const auto found = m_Servers.find(key);

		return found != m_Servers.end() && found->second.OpenDocuments.contains(path);
	}

	void LanguageServices::RestartServer(const Key& key, ManagedServer& server)
	{
		// A dead LspClient's reader and writer threads have already stopped for good — that flag
		// never resets — so the only way back is a new object, never a Reset() on this one.
		// Destroying the old client here is safe: its threads are already joined, so this runs
		// the same teardown a normal destruction would, just without a live process to terminate.
		server.Client.reset();
		server.Client = MakeClient(server, key);

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

	void LanguageServices::ForgetNamedBy(const Key& key)
	{
		std::erase_if(m_NamedBy, [&](const auto& named) { return !(key < named.second) && !(named.second < key); });
	}

	void LanguageServices::Restrict(const Key& key, ManagedServer& server)
	{
		// A restricted server is no authority on any file's root: what it named is judged by its
		// own directory again, the same as when a server is reaped.
		ForgetNamedBy(key);

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
				Restrict(key, server);
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
					   respectGitIgnore] {
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

					enumeration.Ignored = [&repository](const std::filesystem::path& path, bool) { return repository->IsIgnored(path); };
				}
			}

			EnumerateFiles(enumeration, token, [&](const std::filesystem::path& file) {
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
				// class does not do. Bytes as they sit on disk, which is what a server wants: it
				// re-reads the file itself, and this copy only makes it load the compilation
				// database. The ceiling is the editor's, so a warm-up never holds a file the
				// editor would refuse; a file that cannot be read primes the server with nothing.
				Core::Result<std::string> read = Core::ReadFileBytes(file, Core::MaximumFileSize);
				std::string text = read ? std::move(read.GetValue()) : std::string{};

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
			//
			// Skipped when the server already holds that document: a tab open on it - restored at
			// startup, or replayed by Revalidate the moment its folder was trusted - has already
			// sent it and primed the database. Opening it again would overwrite the tab's text with
			// the disk's, and the close would make the server forget the tab, so every edit after
			// it would be dropped as a change to a document it was never told about.
			if (!finding.Text.empty() && !Holds(finding.Path, finding.Language))
			{
				OpenDocument(finding.Path, finding.Language, std::move(finding.Text));
				CloseDocument(finding.Path, finding.Language);
			}
		}

		const std::chrono::steady_clock::time_point now = m_Clock ? m_Clock() : std::chrono::steady_clock::now();

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

				// Every file this server pointed at is remembered against it, so one with no
				// project of its own is served here when it is opened. A map insert per location;
				// nothing touches the filesystem until such a file is actually opened.
				for (const Location& location : answer.Locations)
				{
					NoteNamedByKey(location.Path, key);
				}

				Answer delivered;
				delivered.For = pending->second;
				delivered.Locations = std::move(answer.Locations);
				delivered.Declarations = std::move(answer.Declarations);
				delivered.SemanticTokens = std::move(answer.SemanticTokens);
				delivered.Text = std::move(answer.Text);
				delivered.Failed = answer.Failed;

				m_Answers.push_back(std::move(delivered));
				server.Pending.erase(pending);
			}

			// Moved rather than copied - a publication is one server's word on one document, and
			// Poll is the only place any Drain result is seen, same as drained.Answers above.
			m_Diagnostics.Published.insert(m_Diagnostics.Published.end(),
										   std::make_move_iterator(drained.Diagnostics.begin()),
										   std::make_move_iterator(drained.Diagnostics.end()));

			// A server that stays up without crashing earns the restart budget back, measured on
			// the same clock as the backoff below. Moving between up states (indexing finishing,
			// a compile database appearing) does not restart the clock; only a crash or a
			// restart does.
			const ServerState state = drained.Status.State;
			const bool up = state == ServerState::Ready || state == ServerState::Indexing ||
							state == ServerState::NoCompileDatabase;

			if (up)
			{
				if (!server.UpSince)
				{
					server.UpSince = now;
				}
				else if (RestartBudgetRefilled(server.UpSince, now))
				{
					server.Restarts = 0;
				}
			}
			else
			{
				server.UpSince.reset();
			}

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

			if (!MayRestart(server.Restarts))
			{
				// Settled: kMaximumRestarts attempts have already failed, and Status.State == Failed is
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

		ReapIdle(now);
	}

	void LanguageServices::SetClock(Clock clock)
	{
		m_Clock = std::move(clock);
	}

	void LanguageServices::SetClientFactory(ClientFactory factory)
	{
		m_ClientFactory = std::move(factory);
	}

	Core::Scope<LspClient> LanguageServices::MakeClient(const ManagedServer& server,
														const Key& key) const
	{
		if (m_ClientFactory)
		{
			return m_ClientFactory(*server.Config, server.Binary, key.Root);
		}

		return Core::CreateScope<LspClient>(*server.Config, server.Binary, key.Root);
	}

	bool LanguageServices::IsRunningFor(const std::filesystem::path& path, LanguageId language) const
	{
		Key key;

		return Resolve(path, language, key) != nullptr && m_Servers.contains(key);
	}

	void LanguageServices::ReapIdle(std::chrono::steady_clock::time_point now)
	{
		for (auto it = m_Servers.begin(); it != m_Servers.end();)
		{
			ManagedServer& server = it->second;
			const std::filesystem::path& root = it->first.Root;

			// The workspace's own servers - rooted at the open folder, inside it, or above it when
			// a subfolder of a project is open - hold the background index and are never reaped.
			// Pending is checked even though only a live client can have any: a server is never
			// destroyed with an answer still owed.
			const bool workspaces = Core::IsSameOrWithin(root, m_FallbackRoot) || Core::IsSameOrWithin(m_FallbackRoot, root);
			const bool idle = !workspaces && server.OpenDocuments.empty() && server.Pending.empty();

			if (!idle)
			{
				server.IdleSince.reset();
				++it;
				continue;
			}

			if (!server.IdleSince)
			{
				server.IdleSince = now;
				++it;
				continue;
			}

			if (now - *server.IdleSince < kIdleShutdown)
			{
				++it;
				continue;
			}

			if (server.Client)
			{
				APP_INFO("Stopping {} for {} at {}: unused for {} minutes", server.Status.Name,
						 LanguageDisplayName(it->first.Language), Core::ToUtf8(root),
						 std::chrono::duration_cast<std::chrono::minutes>(kIdleShutdown).count());
				server.Client->RequestShutdown();
			}

			// What this server named goes with it, so the map stays bounded by the servers alive:
			// a file it pointed at is judged by its own directory again, not by a root nothing runs at.
			ForgetNamedBy(it->first);

			// Erasing the entry is the whole shutdown: the client's destructor ends the process,
			// and the next EnsureStarted for this root starts a fresh one, as for a first open.
			it = m_Servers.erase(it);
		}
	}
}

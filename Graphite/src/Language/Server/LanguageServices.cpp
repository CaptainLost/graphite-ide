#include "Language/Server/LanguageServices.h"

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Text/FileProbe.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/ProjectSearch.h"
#include "Language/Server/ServerCommand.h"
#include "Language/Server/ServerLaunch.h"
#include "Language/Server/SolutionForRoot.h"
#include "Language/Server/SolutionScope.h"
#include "Git/GitIgnoreRules.h"
#include "Search/FileEnumerator.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr long long kBackoffSecondsPerRestart = 2;
		constexpr std::size_t kNoRestarts = 0;
		constexpr std::uint64_t kNoProjectSearch = 0;
		constexpr RequestId kDeclinedRequest = 0;

		constexpr int kRankInProgress = 4;
		constexpr int kRankBroken = 3;
		constexpr int kRankReady = 2;
		constexpr int kRankRestricted = 1;
		constexpr int kRankSilent = 0;

		bool ContainsLanguage(const std::vector<LanguageId>& languages, LanguageId language)
		{
			return std::find(languages.begin(), languages.end(), language) != languages.end();
		}

		ServerStatus WithLanguage(ServerStatus status, LanguageId language)
		{
			status.Language = language;
			return status;
		}

		int RankOf(ServerState state)
		{
			switch (state)
			{
			case ServerState::Indexing:
			case ServerState::Starting:
				return kRankInProgress;
			case ServerState::Failed:
			case ServerState::NotFound:
			case ServerState::NotInstalled:
			case ServerState::MissingRuntime:
			case ServerState::ProjectIncomplete:
				return kRankBroken;
			case ServerState::Ready:
				return kRankReady;
			case ServerState::Restricted:
				return kRankRestricted;
			case ServerState::Off:
			case ServerState::NotConfigured:
				return kRankSilent;
			}

			return kRankSilent;
		}
	}

	std::chrono::steady_clock::duration RestartBackoff(std::size_t restarts)
	{
		return std::chrono::seconds(static_cast<long long>(restarts) * kBackoffSecondsPerRestart);
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
		std::error_code noTemporaryDirectory;
		m_LogDirectory = std::filesystem::temp_directory_path(noTemporaryDirectory);
	}

	LanguageServices::~LanguageServices()
	{
		m_WarmToken.Cancel();
		m_ProjectSearchToken.Cancel();

		for (auto& [key, server] : m_Servers)
		{
			Retire(std::move(server.Client));
		}
	}

	bool LanguageServices::WarmUpNeedsOpen(LanguageId language) const
	{
		const ServerDefinition* server = ServerFor(language);

		return server != nullptr && AdapterOf(server->Adapter).WarmUpNeedsOpen();
	}

	const ServerDefinition* LanguageServices::ServerFor(LanguageId language) const
	{
		const ServerChoice& choice = ChoiceFor(language);

		return choice.Kind == ServerChoiceKind::Chosen ? choice.Server : nullptr;
	}

	LanguageServices::ManagedServer* LanguageServices::FindManagedServer(const ServerKey& key)
	{
		const ServerMap::iterator found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return nullptr;
		}

		auto& [foundKey, managed] = *found;

		return &managed;
	}

	const LanguageServices::ServerKey* LanguageServices::KeyOf(const KeyByPath& keys, const std::filesystem::path& path,
															   LanguageId language)
	{
		const KeyByPath::const_iterator found = keys.find(path);

		if (found == keys.end())
		{
			return nullptr;
		}

		const auto& [foundPath, foundKey] = *found;

		return foundKey.Language == language ? &foundKey : nullptr;
	}

	LanguageServices::ParkedDocument* LanguageServices::FindParkedDocument(const std::filesystem::path& path,
																		   LanguageId language)
	{
		const ParkedMap::iterator found = m_Parked.find(path);

		if (found == m_Parked.end())
		{
			return nullptr;
		}

		auto& [foundPath, parked] = *found;

		return parked.Language == language ? &parked : nullptr;
	}

	const ServerDefinition* LanguageServices::Resolve(const std::filesystem::path& path, LanguageId language,
													  ServerKey& key) const
	{
		const ServerDefinition* server = ServerFor(language);

		if (server == nullptr)
		{
			return nullptr;
		}

		key.Language = language;

		const ServerKey* openDocumentKey = KeyOf(m_DocumentKeys, path, language);

		if (openDocumentKey != nullptr)
		{
			key.Root = openDocumentKey->Root;
			return server;
		}

		const ServerKey* namingServerKey = KeyOf(m_NamedBy, path, language);

		if (namingServerKey != nullptr)
		{
			const bool inFolder = Core::IsSameOrWithin(path, m_FallbackRoot);

			key.Root = RootFor(*server, path, inFolder ? m_FallbackRoot : namingServerKey->Root);
			return server;
		}

		const std::pair<LanguageId, std::filesystem::path> cacheKey = std::make_pair(language, path.parent_path());
		const RootCacheMap::const_iterator cached = m_RootCache.find(cacheKey);

		if (cached != m_RootCache.end())
		{
			const auto& [cachedKey, cachedRoot] = *cached;

			key.Root = cachedRoot;
			return server;
		}

		key.Root = RootFor(*server, path, FallbackFor(path));
		m_RootCache.emplace(cacheKey, key.Root);

		return server;
	}

	std::filesystem::path LanguageServices::FallbackFor(const std::filesystem::path& path) const
	{
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
		ServerKey key;

		if (Resolve(by, language, key) != nullptr)
		{
			NoteNamedByKey(named, key);
		}
	}

	void LanguageServices::NoteNamedByKey(const std::filesystem::path& named, const ServerKey& key)
	{
		if (!m_DocumentKeys.contains(named))
		{
			m_NamedBy[named] = key;
		}
	}

	void LanguageServices::SetChoiceProvider(ChoiceProvider provider)
	{
		m_ChoiceProvider = std::move(provider);
		m_Choices.clear();
	}

	void LanguageServices::Reconfigure(LanguageId language)
	{
		std::vector<HeldDocument> held = ReleaseServersOf(language);

		m_Choices.erase(language);
		m_Dotnet.reset();
		ForgetRootsOf(language);

		for (HeldDocument& parked : TakeParkedDocumentsOf(language))
		{
			held.push_back(std::move(parked));
		}

		for (HeldDocument& document : held)
		{
			EnsureStarted(document.Path, language);
			OpenDocument(document.Path, language, std::move(document.Text), document.Role);
		}

		if (HasChosenServer(language))
		{
			WarmOpenFolderAgain();
		}
	}

	void LanguageServices::WarmOpenFolderAgain()
	{
		const bool warmedFolderIsOpen = m_LastWarmUp.has_value() && Core::IsSameOrWithin(m_LastWarmUp->Root, m_FallbackRoot) &&
										Core::IsSameOrWithin(m_FallbackRoot, m_LastWarmUp->Root);

		if (warmedFolderIsOpen)
		{
			WarmUp(m_LastWarmUp->Root, m_LastWarmUp->RespectGitIgnore);
		}
	}

	bool LanguageServices::HasChosenServer(LanguageId language) const
	{
		return ChoiceFor(language).Kind == ServerChoiceKind::Chosen;
	}

	const ServerDefinition* LanguageServices::ChosenServer(LanguageId language) const
	{
		return ServerFor(language);
	}

	std::optional<ServerCommand> LanguageServices::FoundCommandFor(LanguageId language) const
	{
		for (const auto& [key, server] : m_Servers)
		{
			if (key.Language == language && server.Client != nullptr)
			{
				return server.Command;
			}
		}

		return std::nullopt;
	}

	bool LanguageServices::OffersSwitchSourceHeader(LanguageId language) const
	{
		const ServerDefinition* server = ServerFor(language);

		return server != nullptr && AdapterOf(server->Adapter).SupportsSwitchSourceHeader();
	}

	std::vector<LanguageServices::HeldDocument> LanguageServices::TakeParkedDocumentsOf(LanguageId language)
	{
		std::vector<HeldDocument> taken;

		for (ParkedMap::iterator entry = m_Parked.begin(); entry != m_Parked.end();)
		{
			auto& [path, parked] = *entry;

			if (parked.Language != language)
			{
				++entry;
				continue;
			}

			taken.push_back(HeldDocument{.Path = path, .Text = std::move(parked.Text), .Role = parked.Role});
			entry = m_Parked.erase(entry);
		}

		return taken;
	}

	std::vector<LanguageServices::HeldDocument> LanguageServices::ReleaseServersOf(LanguageId language)
	{
		std::vector<HeldDocument> held;

		for (ServerMap::iterator entry = m_Servers.begin(); entry != m_Servers.end();)
		{
			auto& [key, server] = *entry;

			if (key.Language != language)
			{
				++entry;
				continue;
			}

			for (auto& [path, document] : server.OpenDocuments)
			{
				held.push_back(HeldDocument{.Path = path, .Text = std::move(document.Text), .Role = document.Role});
				m_DocumentKeys.erase(path);

				if (server.Client)
				{
					m_Diagnostics.Closed.push_back(path);
				}
			}

			FailPending(server, ReplyFailure::ServerUnavailable, key.Language);

			Retire(std::move(server.Client));

			entry = m_Servers.erase(entry);
		}

		return held;
	}

	bool LanguageServices::IsWorkspaceRoot(const std::filesystem::path& root) const
	{
		return Core::IsSameOrWithin(root, m_FallbackRoot) || Core::IsSameOrWithin(m_FallbackRoot, root);
	}

	void LanguageServices::ForgetRootsOf(LanguageId language)
	{
		for (RootCacheMap::iterator cached = m_RootCache.begin(); cached != m_RootCache.end();)
		{
			const auto& [cacheKey, root] = *cached;
			const auto& [cachedLanguage, directory] = cacheKey;

			if (cachedLanguage == language)
			{
				cached = m_RootCache.erase(cached);
				continue;
			}

			++cached;
		}
	}

	const ServerChoice& LanguageServices::ChoiceFor(LanguageId language) const
	{
		if (!m_Choices.contains(language))
		{
			const std::optional<std::string> nothingStored;
			ServerChoice choice = m_ChoiceProvider ? m_ChoiceProvider(language) : ChooseServer(language, nothingStored);

			m_Choices.emplace(language, std::move(choice));
		}

		return m_Choices.at(language);
	}

	void LanguageServices::SetServersDirectory(std::filesystem::path directory)
	{
		m_ServersDirectory = std::move(directory);
	}

	void LanguageServices::SetDotnetLocator(DotnetLocator locator)
	{
		m_DotnetLocator = std::move(locator);
		m_Dotnet.reset();
	}

	void LanguageServices::SetUninstallingQuery(UninstallingQuery query)
	{
		m_Uninstalling = std::move(query);
	}

	void LanguageServices::SetLogDirectory(std::filesystem::path directory)
	{
		m_LogDirectory = std::move(directory);
	}

	void LanguageServices::SetTemporaryFilesDirectory(std::filesystem::path directory)
	{
		Core::Result<ClaimedTemporaryDirectory> claimed = ClaimedTemporaryDirectory::Claim(directory);

		if (claimed)
		{
			m_TemporaryFiles.emplace(std::move(claimed.GetValue()));
		}
		else
		{
			APP_WARN("Language servers keep their temporary files where the system does: {}", claimed.GetError().Message);
		}

		// WARNING: Submitted after the claim, whose held lock is what keeps this Graphite's own directory out of the sweep.
		m_Jobs.Submit([directory] { SweepUnclaimedTemporaryDirectories(directory); });
	}

	void LanguageServices::Retire(Core::Scope<LspClient> client)
	{
		if (!client)
		{
			return;
		}

		const Core::Ref<LspClient> leaving{std::move(client)};

		m_Jobs.Submit([leaving] { leaving->ShutDown(LspClient::kShutdownGrace); });
	}

	void LanguageServices::SetSelectedSolution(const std::filesystem::path& solution)
	{
		if (solution == m_SelectedSolution)
		{
			return;
		}

		m_SelectedSolution = solution;

		bool warmsAgain = false;

		for (const LanguageId language : LanguagesWithServers())
		{
			if (FollowsSelectedSolution(language) && RunsServersOf(language))
			{
				Reconfigure(language);
			}
			else if (LanguageRootsAtSolution(language) && HasChosenServer(language))
			{
				warmsAgain = true;
			}
		}

		if (warmsAgain)
		{
			WarmOpenFolderAgain();
		}
	}

	bool LanguageServices::FollowsSelectedSolution(LanguageId language) const
	{
		const ServerDefinition* server = ServerFor(language);

		return server != nullptr && AdapterOf(server->Adapter).FollowsSelectedSolution();
	}

	bool LanguageServices::RunsServersOf(LanguageId language) const
	{
		for (const auto& [key, server] : m_Servers)
		{
			if (key.Language == language)
			{
				return true;
			}
		}

		return false;
	}

	const std::vector<DotnetInstallation>& LanguageServices::DotnetInstallations() const
	{
		if (!m_Dotnet)
		{
			m_Dotnet = m_DotnetLocator ? m_DotnetLocator() : FindDotnetInstallations(DotnetCandidateRoots());
		}

		return *m_Dotnet;
	}

	ServerCommandPlaces LanguageServices::CommandPlaces() const
	{
		return ServerCommandPlaces{.ServersDirectory = m_ServersDirectory, .ListDotnet = [this] { return DotnetInstallations(); }};
	}

	ServerStatus LanguageServices::OffStatus(LanguageId language) const
	{
		return ServerStatus{.State = ServerState::Off, .Name = std::string{LanguageDisplayName(language)}, .Language = language};
	}

	void LanguageServices::StartServer(ManagedServer& server, const ServerKey& key)
	{
		server.Status.Name = std::string(server.Definition->DisplayName);
		server.Status.Restarts = server.Restarts;

		if (m_Uninstalling && m_Uninstalling(server.Definition->Id))
		{
			server.Status.State = ServerState::NotInstalled;
			server.Status.Detail = "it is being uninstalled";
			return;
		}

		server.Command = FindServerCommand(*server.Definition, server.Overrides.Binary, CommandPlaces());

		if (server.Command.State != ServerState::Starting)
		{
			server.Status.State = server.Command.State;
			server.Status.Detail = server.Command.Detail;
			NoteStateEntered(server, "cannot start: " + server.Command.Detail);

			APP_WARN("Cannot start {} for {}: {}", server.Definition->DisplayName, LanguageDisplayName(key.Language),
					 server.Command.Detail);
			return;
		}

		APP_INFO("Starting {} for {} at {}: {}", server.Definition->DisplayName, LanguageDisplayName(key.Language),
				 Core::ToUtf8(key.Root), DescribeCommand(server.Command));

		if (!server.Command.InstalledFile.empty())
		{
			if (Core::Result<Core::FileHold> held = Core::FileHold::HoldOpen(server.Command.InstalledFile))
			{
				server.Hold.emplace(std::move(held.GetValue()));
			}
			else
			{
				APP_WARN("Cannot hold {} open while it runs: {}", Core::ToUtf8(server.Command.InstalledFile), held.GetError().Message);
			}
		}

		if (NeedsProjectSearch(server, key))
		{
			BeginProjectSearch(server, key);
			return;
		}

		server.Client = MakeClient(server, key);

		server.Status.State = ServerState::Starting;
		server.Status.Detail = DescribeCommand(server.Command);
		NoteStateEntered(server, "starting");
	}

	void LanguageServices::EnsureStarted(const std::filesystem::path& path, LanguageId language)
	{
		ServerKey key;
		const ServerDefinition* definition = Resolve(path, language, key);

		if (definition == nullptr)
		{
			return;
		}

		if (m_Servers.contains(key))
		{
			return;
		}

		ManagedServer server;
		server.Definition = definition;
		server.Overrides = ChoiceFor(language).Overrides;
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
		if (ChoiceFor(language).Kind == ServerChoiceKind::Off)
		{
			return OffStatus(language);
		}

		for (const auto& [key, server] : m_Servers)
		{
			if (key.Language == language)
			{
				return WithLanguage(server.Status, language);
			}
		}

		return ServerStatus{.Language = language};
	}

	ServerStatus LanguageServices::StatusFor(const std::filesystem::path& path, LanguageId language)
	{
		if (ChoiceFor(language).Kind == ServerChoiceKind::Off)
		{
			return OffStatus(language);
		}

		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			return ServerStatus{.Language = language};
		}

		const ManagedServer* found = FindManagedServer(key);

		return found != nullptr ? WithLanguage(found->Status, language) : ServerStatus{.Language = language};
	}

	std::optional<ServerStatus> LanguageServices::OverallStatus() const
	{
		const ServerStatus* best = nullptr;
		LanguageId bestLanguage = LanguageId::PlainText;

		for (const auto& [key, server] : m_Servers)
		{
			const int serverRank = RankOf(server.Status.State);

			if (serverRank == kRankSilent)
			{
				continue;
			}

			if (best == nullptr || serverRank > RankOf(best->State))
			{
				best = &server.Status;
				bestLanguage = key.Language;
			}
		}

		return best != nullptr ? std::optional<ServerStatus>{WithLanguage(*best, bestLanguage)} : std::nullopt;
	}

	LanguageServices::Ticket LanguageServices::IssueTicket()
	{
		return static_cast<Ticket>(m_NextTicket++);
	}

	void LanguageServices::Fail(Ticket ticket, ReplyFailure reason, ServerStatus server)
	{
		Answer answer;
		answer.For = ticket;
		answer.Failed = true;
		answer.FailedBecause = reason;
		answer.ServerAtFailure = std::move(server);

		m_Answers.push_back(std::move(answer));
	}

	void LanguageServices::FailPending(ManagedServer& server, ReplyFailure reason, LanguageId language)
	{
		for (const auto& [requestId, ticket] : server.Pending)
		{
			Fail(ticket, reason, WithLanguage(server.Status, language));
		}

		server.Pending.clear();
	}

	LanguageServices::Ticket LanguageServices::AskVia(
		const std::filesystem::path& path, LanguageId language,
		const std::function<RequestId(LspClient&)>& ask)
	{
		const Ticket ticket = IssueTicket();

		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			Fail(ticket, ReplyFailure::ServerUnavailable, StatusFor(language));
			return ticket;
		}

		ManagedServer* server = FindManagedServer(key);

		if (server == nullptr)
		{
			Fail(ticket, ReplyFailure::ServerUnavailable, ServerStatus{.Language = language});
			return ticket;
		}

		if (!server->Client)
		{
			Fail(ticket, ReplyFailure::ServerUnavailable, WithLanguage(server->Status, language));
			return ticket;
		}

		const RequestId requestId = ask(*server->Client);

		if (requestId == kDeclinedRequest)
		{
			Fail(ticket, ReplyFailure::ServerError, WithLanguage(server->Status, language));
			return ticket;
		}

		server->Pending[requestId] = ticket;

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

	void LanguageServices::TakeMessages(std::vector<ServerMessage>& messages)
	{
		messages.insert(messages.end(), std::make_move_iterator(m_Messages.begin()), std::make_move_iterator(m_Messages.end()));
		m_Messages.clear();
	}

	void LanguageServices::TakeRecolour(std::vector<std::filesystem::path>& paths)
	{
		paths.insert(paths.end(), std::make_move_iterator(m_Recolour.begin()), std::make_move_iterator(m_Recolour.end()));
		m_Recolour.clear();
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
		update.Unanswered.insert(update.Unanswered.end(),
								 std::make_move_iterator(m_Diagnostics.Unanswered.begin()),
								 std::make_move_iterator(m_Diagnostics.Unanswered.end()));

		m_Diagnostics.Published.clear();
		m_Diagnostics.Versions.clear();
		m_Diagnostics.Closed.clear();
		m_Diagnostics.Unanswered.clear();
	}

	void LanguageServices::Cancel(Ticket ticket)
	{
		for (auto& [key, server] : m_Servers)
		{
			for (ManagedServer::PendingTickets::iterator pending = server.Pending.begin(); pending != server.Pending.end(); ++pending)
			{
				const auto& [requestId, pendingTicket] = *pending;

				if (pendingTicket != ticket)
				{
					continue;
				}

				if (server.Client)
				{
					server.Client->Cancel(requestId);
				}

				server.Pending.erase(pending);
				return;
			}
		}
	}

	void LanguageServices::OpenDocument(const std::filesystem::path& path, LanguageId language,
										std::string text, DocumentRole role)
	{
		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			if (LanguageHasServers(language))
			{
				m_Parked[path] = ParkedDocument{.Language = language, .Text = std::move(text), .Role = role};
			}

			return;
		}

		m_Parked.erase(path);

		ManagedServer* found = FindManagedServer(key);

		if (found == nullptr)
		{
			return;
		}

		m_DocumentKeys[path] = key;

		ManagedServer& server = *found;

		ManagedServer::SentDocument& kept = server.OpenDocuments[path];
		kept.Text = std::move(text);
		kept.Role = role;

		if (server.Client)
		{
			kept.Version = server.Client->OpenDocument(path, kept.Text, kept.Role);
			m_Diagnostics.Versions.push_back(DocumentVersionNote{path, kept.Version});
		}
	}

	void LanguageServices::ChangeDocument(const std::filesystem::path& path, LanguageId language,
										  std::string text)
	{
		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			ParkedDocument* parked = FindParkedDocument(path, language);

			if (parked != nullptr)
			{
				parked->Text = std::move(text);
			}

			return;
		}

		ManagedServer* found = FindManagedServer(key);

		if (found == nullptr)
		{
			return;
		}

		ManagedServer& server = *found;

		const std::map<std::filesystem::path, ManagedServer::SentDocument>::iterator openDocument = server.OpenDocuments.find(path);

		if (openDocument == server.OpenDocuments.end())
		{
			return;
		}

		auto& [documentPath, document] = *openDocument;

		document.Text = std::move(text);

		if (server.Client)
		{
			document.Version = server.Client->ChangeDocument(path, document.Text);

			if (document.Version != DocumentVersion{})
			{
				m_Diagnostics.Versions.push_back(DocumentVersionNote{path, document.Version});
			}
		}
	}

	void LanguageServices::SaveDocument(const std::filesystem::path& path, LanguageId language)
	{
		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			return;
		}

		ManagedServer* found = FindManagedServer(key);

		if (found == nullptr)
		{
			return;
		}

		ManagedServer& server = *found;

		if (!server.OpenDocuments.contains(path))
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
		m_Parked.erase(path);

		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			return;
		}

		m_DocumentKeys.erase(path);

		ManagedServer* found = FindManagedServer(key);

		if (found == nullptr)
		{
			return;
		}

		ManagedServer& server = *found;

		if (server.OpenDocuments.erase(path) == 0)
		{
			return;
		}

		if (server.Client)
		{
			m_Diagnostics.Closed.push_back(path);
			server.Client->CloseDocument(path);
		}
	}

	bool LanguageServices::Holds(const std::filesystem::path& path, LanguageId language)
	{
		ServerKey key;

		if (Resolve(path, language, key) == nullptr)
		{
			return FindParkedDocument(path, language) != nullptr;
		}

		const ManagedServer* found = FindManagedServer(key);

		return found != nullptr && found->OpenDocuments.contains(path);
	}

	void LanguageServices::RestartServer(const ServerKey& key, ManagedServer& server)
	{
		server.Client.reset();
		server.Client = MakeClient(server, key);

		ReplayOpenDocuments(server);

		server.Restarts += 1;
		server.RestartPending = false;

		server.Status.State = ServerState::Starting;
		server.Status.Detail = DescribeCommand(server.Command);
		server.Status.Restarts = server.Restarts;
		NoteStateEntered(server, "restarting");
	}

	std::chrono::steady_clock::time_point LanguageServices::Now() const
	{
		return m_Clock ? m_Clock() : std::chrono::steady_clock::now();
	}

	void LanguageServices::NoteEvent(ManagedServer& server, std::string text, std::optional<std::chrono::seconds> took)
	{
		server.History.push_back(ServerEvent{.At = std::chrono::system_clock::now(), .Text = std::move(text), .Took = took});

		while (server.History.size() > kServerHistoryLength)
		{
			server.History.pop_front();
		}
	}

	void LanguageServices::NoteStateEntered(ManagedServer& server, std::string text, std::optional<std::chrono::seconds> took)
	{
		server.StateSince = Now();
		NoteEvent(server, std::move(text), took);
	}

	// WARNING: Called before server.Status takes `next`, so the state left is still in server.Status.
	void LanguageServices::NoteStateChange(ManagedServer& server, const ServerStatus& next)
	{
		const bool leavesIndexing = server.Status.State == ServerState::Indexing;
		std::optional<std::chrono::seconds> indexedFor;

		if (leavesIndexing && server.StateSince)
		{
			indexedFor = std::chrono::duration_cast<std::chrono::seconds>(Now() - *server.StateSince);
		}

		switch (next.State)
		{
		case ServerState::Starting:
			NoteStateEntered(server, "starting");
			return;

		case ServerState::Indexing:
			NoteStateEntered(server, next.Loading.empty() ? std::string{"indexing"} : "loading " + next.Loading);
			return;

		case ServerState::Ready:
			NoteStateEntered(server, leavesIndexing ? "finished indexing" : "ready", indexedFor);
			return;

		case ServerState::ProjectIncomplete:
			NoteStateEntered(server, "limited: " + next.Problem.Summary, indexedFor);
			return;

		case ServerState::Failed:
			NoteStateEntered(server, next.Detail.empty() ? std::string{"failed"} : "failed: " + next.Detail);
			return;

		case ServerState::NotConfigured:
		case ServerState::Off:
		case ServerState::NotFound:
		case ServerState::NotInstalled:
		case ServerState::MissingRuntime:
		case ServerState::Restricted:
			break;
		}

		server.StateSince = Now();
	}

	std::optional<ServerReport> LanguageServices::ReportFor(const std::filesystem::path& path, LanguageId language) const
	{
		ServerKey key;

		if (ChoiceFor(language).Kind == ServerChoiceKind::Off || Resolve(path, language, key) == nullptr)
		{
			return std::nullopt;
		}

		const ServerMap::const_iterator found = m_Servers.find(key);

		if (found == m_Servers.end())
		{
			return std::nullopt;
		}

		const ManagedServer& server = found->second;

		ServerReport report{.Status = WithLanguage(server.Status, language),
							.Root = key.Root,
							.OpenDocuments = server.OpenDocuments.size(),
							.RequestsInFlight = server.Pending.size(),
							.History = {server.History.begin(), server.History.end()}};

		if (server.StateSince)
		{
			report.InStateFor = std::chrono::duration_cast<std::chrono::seconds>(Now() - *server.StateSince);
		}

		return report;
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
		server.Status.Name = std::string(server.Definition->DisplayName);
		server.Status.State = ServerState::Restricted;
		server.Status.Detail = Core::ToUtf8(server.Root);
		server.Status.Percent = kNoPercentReported;
		server.Status.Restarts = kNoRestarts;
	}

	void LanguageServices::ForgetNamedBy(const ServerKey& key)
	{
		for (KeyByPath::iterator entry = m_NamedBy.begin(); entry != m_NamedBy.end();)
		{
			const auto& [namedPath, namingKey] = *entry;
			const bool namedByThisServer = !(key < namingKey) && !(namingKey < key);

			if (namedByThisServer)
			{
				entry = m_NamedBy.erase(entry);
				continue;
			}

			++entry;
		}
	}

	void LanguageServices::Restrict(const ServerKey& key, ManagedServer& server)
	{
		ForgetNamedBy(key);

		if (server.Client)
		{
			for (const auto& [path, document] : server.OpenDocuments)
			{
				m_Diagnostics.Closed.push_back(path);
			}

			Retire(std::move(server.Client));
		}

		server.Restarts = kNoRestarts;
		server.RestartPending = false;

		server.ProjectSearch = kNoProjectSearch;
		MarkRestricted(server);

		FailPending(server, ReplyFailure::ServerUnavailable, key.Language);
	}

	void LanguageServices::ReplayOpenDocuments(ManagedServer& server)
	{
		for (auto& [path, document] : server.OpenDocuments)
		{
			document.Version = server.Client->OpenDocument(path, document.Text, document.Role);

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

	void LanguageServices::RunWarmUpWalk(const Core::Ref<WarmFindings>& warm, const Core::CancellationToken& token,
										 const std::filesystem::path& root, const std::filesystem::path& selectedSolution,
										 bool respectGitIgnore, const std::vector<LanguageId>& wanted)
	{
		std::vector<LanguageId> seen;

		EnumerationInput enumeration;
		enumeration.Root = root;

		SolutionScope scope{root, selectedSolution};

		std::optional<GitIgnoreRules> gitIgnore;

		if (respectGitIgnore)
		{
			gitIgnore.emplace(root);
			enumeration.Ignored = [&gitIgnore](const std::filesystem::path& path, bool) { return gitIgnore->IsIgnored(path); };
		}

		EnumerateFiles(enumeration, token, [&](const std::filesystem::path& file) {
			if (seen.size() == wanted.size())
			{
				token.Cancel();
				return;
			}

			const LanguageId language = LanguageOfPath(file);

			if (!ContainsLanguage(wanted, language) ||
				std::find(seen.begin(), seen.end(), language) != seen.end())
			{
				return;
			}

			if (LanguageRootsAtSolution(language) && !scope.Includes(file))
			{
				return;
			}

			seen.push_back(language);

			Core::Result<std::string> read = Core::ReadFileBytes(file, Core::MaximumFileSize);
			std::string text = read ? std::move(read.GetValue()) : std::string{};

			warm->Post(WarmFinding{language, file, std::move(text)});
		});
	}

	void LanguageServices::WarmUp(std::filesystem::path root, bool respectGitIgnore)
	{
		m_LastWarmUp = WarmUpRequest{.Root = root, .RespectGitIgnore = respectGitIgnore};

		m_WarmToken.Cancel();
		m_WarmToken = Core::CancellationToken{};

		m_Warm->Clear();

		if (!Permits(root))
		{
			return;
		}

		std::vector<LanguageId> wanted;

		for (const LanguageId language : LanguagesWithServers())
		{
			if (HasChosenServer(language))
			{
				wanted.push_back(language);
			}
		}

		if (wanted.empty())
		{
			return;
		}

		m_Jobs.Submit([warm = m_Warm, token = m_WarmToken, root = std::move(root), selectedSolution = m_SelectedSolution,
					   respectGitIgnore, wanted = std::move(wanted)] {
			RunWarmUpWalk(warm, token, root, selectedSolution, respectGitIgnore, wanted);
		});
	}

	bool LanguageServices::NeedsProjectSearch(const ManagedServer& server, const ServerKey& key) const
	{
		return !server.Projects && AdapterOf(server.Definition->Adapter).ProjectFileExtension().has_value() &&
			   !SolutionForRoot(key.Root, m_SelectedSolution).has_value();
	}

	void LanguageServices::BeginProjectSearch(ManagedServer& server, const ServerKey& key)
	{
		server.ProjectSearch = m_NextProjectSearch++;
		server.Status.State = ServerState::Starting;
		server.Status.Detail = "looking for projects";
		NoteStateEntered(server, "looking for projects");

		APP_INFO("Looking for projects for {} below {}", server.Definition->DisplayName, Core::ToUtf8(key.Root));

		const std::string extension{*AdapterOf(server.Definition->Adapter).ProjectFileExtension()};

		m_Jobs.Submit([searches = m_ProjectSearches, token = m_ProjectSearchToken, key, search = server.ProjectSearch, extension] {
			ProjectsBelow projects = FindProjectsBelow(key.Root, extension, kMostProjectsLoaded, token);

			if (token.IsCancelled())
			{
				return;
			}

			searches->Post(FoundProjects{.Server = key, .Search = search, .Projects = std::move(projects)});
		});
	}

	void LanguageServices::StartSearchedServers()
	{
		std::vector<FoundProjects> found = m_ProjectSearches->TakeAll();

		for (FoundProjects& search : found)
		{
			ManagedServer* entry = FindManagedServer(search.Server);

			if (entry == nullptr || entry->ProjectSearch != search.Search)
			{
				continue;
			}

			ManagedServer& server = *entry;
			const std::string root = Core::ToUtf8(search.Server.Root);
			const std::size_t keptCount = search.Projects.Kept.size();
			const std::size_t droppedCount = search.Projects.DroppedAboveLimit;

			if (droppedCount == 0)
			{
				APP_INFO("Found {} projects for {} below {}", keptCount, server.Definition->DisplayName, root);
			}
			else
			{
				APP_WARN("Found {} projects for {} below {}, loading {} and leaving out {} past the limit", keptCount + droppedCount,
						 server.Definition->DisplayName, root, keptCount, droppedCount);
			}

			server.ProjectSearch = kNoProjectSearch;
			NoteEvent(server, "found " + Core::Counted(keptCount + droppedCount, "project"));
			server.Projects = std::move(search.Projects.Kept);
			server.Client = MakeClient(server, search.Server);
			server.Status.Detail = DescribeCommand(server.Command);
			NoteEvent(server, "starting");

			ReplayOpenDocuments(server);
		}
	}

	void LanguageServices::Poll()
	{
		std::vector<WarmFinding> warmFindings = m_Warm->TakeAll();

		for (WarmFinding& finding : warmFindings)
		{
			EnsureStarted(finding.Path, finding.Language);

			if (!finding.Text.empty() && !Holds(finding.Path, finding.Language) && WarmUpNeedsOpen(finding.Language))
			{
				OpenDocument(finding.Path, finding.Language, std::move(finding.Text));
				CloseDocument(finding.Path, finding.Language);
			}
		}

		StartSearchedServers();

		const std::chrono::steady_clock::time_point now = m_Clock ? m_Clock() : std::chrono::steady_clock::now();

		for (auto& [key, server] : m_Servers)
		{
			if (!server.Client)
			{
				continue;
			}

			LspClient::Drained drained;
			server.Client->Drain(drained);

			for (LspClient::Answer& answer : drained.Answers)
			{
				const ManagedServer::PendingTickets::iterator pending = server.Pending.find(answer.Id);

				if (pending == server.Pending.end())
				{
					continue;
				}

				const auto& [pendingId, pendingTicket] = *pending;

				for (const Location& location : answer.Locations)
				{
					NoteNamedByKey(location.Path, key);
				}

				Answer delivered;
				static_cast<ServerReply&>(delivered) = std::move(static_cast<ServerReply&>(answer));
				delivered.For = pendingTicket;

				if (delivered.Failed)
				{
					delivered.ServerAtFailure = WithLanguage(drained.Status, key.Language);
				}

				m_Answers.push_back(std::move(delivered));
				server.Pending.erase(pending);
			}

			m_Diagnostics.Published.insert(m_Diagnostics.Published.end(),
										   std::make_move_iterator(drained.Diagnostics.begin()),
										   std::make_move_iterator(drained.Diagnostics.end()));

			m_Diagnostics.Unanswered.insert(m_Diagnostics.Unanswered.end(),
											std::make_move_iterator(drained.Unanswered.begin()),
											std::make_move_iterator(drained.Unanswered.end()));

			m_Messages.insert(m_Messages.end(), std::make_move_iterator(drained.Messages.begin()),
							  std::make_move_iterator(drained.Messages.end()));

			m_Recolour.insert(m_Recolour.end(), std::make_move_iterator(drained.Recolour.begin()),
							  std::make_move_iterator(drained.Recolour.end()));

			const ServerState state = drained.Status.State;
			const bool isUp = state == ServerState::Ready || state == ServerState::Indexing ||
							  state == ServerState::ProjectIncomplete;

			if (isUp)
			{
				if (!server.UpSince)
				{
					server.UpSince = now;
				}
				else if (RestartBudgetRefilled(server.UpSince, now))
				{
					server.Restarts = kNoRestarts;
				}
			}
			else
			{
				server.UpSince.reset();
			}

			drained.Status.Restarts = server.Restarts;

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

				NoteStateChange(server, drained.Status);
			}

			server.Status = drained.Status;

			if (drained.Status.State != ServerState::Failed)
			{
				server.RestartPending = false;
				continue;
			}

			FailPending(server, ReplyFailure::ServerUnavailable, key.Language);

			if (!MayRestart(server.Restarts))
			{
				continue;
			}

			if (!server.RestartPending)
			{
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
														const ServerKey& key) const
	{
		const std::filesystem::path temporaryDirectory = m_TemporaryFiles ? m_TemporaryFiles->GetPath() : std::filesystem::path{};

		ServerLaunch launch = LaunchFor(*server.Definition, server.Command.Program, server.Overrides,
										LaunchPlaces{.LeadingArguments = server.Command.LeadingArguments, .LogDirectory = m_LogDirectory, .DotnetRoot = server.Command.DotnetRoot, .TemporaryDirectory = temporaryDirectory});

		launch.Solution = SolutionForRoot(key.Root, m_SelectedSolution);

		if (server.Projects)
		{
			launch.Projects = *server.Projects;
		}

		if (m_ClientFactory)
		{
			return m_ClientFactory(launch, key.Root);
		}

		return Core::CreateScope<LspClient>(launch, key.Root);
	}

	bool LanguageServices::IsRunningFor(const std::filesystem::path& path, LanguageId language) const
	{
		ServerKey key;

		return Resolve(path, language, key) != nullptr && m_Servers.contains(key);
	}

	void LanguageServices::ReapIdle(std::chrono::steady_clock::time_point now)
	{
		for (ServerMap::iterator entry = m_Servers.begin(); entry != m_Servers.end();)
		{
			auto& [key, server] = *entry;
			const std::filesystem::path& root = key.Root;

			const bool isWorkspace = IsWorkspaceRoot(root);
			const bool isIdle = !isWorkspace && server.OpenDocuments.empty() && server.Pending.empty();

			if (!isIdle)
			{
				server.IdleSince.reset();
				++entry;
				continue;
			}

			if (!server.IdleSince)
			{
				server.IdleSince = now;
				++entry;
				continue;
			}

			if (now - *server.IdleSince < kIdleShutdown)
			{
				++entry;
				continue;
			}

			if (server.Client)
			{
				APP_INFO("Stopping {} for {} at {}: unused for {} minutes", server.Status.Name,
						 LanguageDisplayName(key.Language), Core::ToUtf8(root),
						 std::chrono::duration_cast<std::chrono::minutes>(kIdleShutdown).count());
				Retire(std::move(server.Client));
			}

			ForgetNamedBy(key);

			entry = m_Servers.erase(entry);
		}
	}
}

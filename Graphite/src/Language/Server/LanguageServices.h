#pragma once

#include "Core/Base/Base.h"

#include "Language/Api/HoverInfo.h"
#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/ClaimedTemporaryDirectory.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/LspClient.h"
#include "Language/Server/ProjectSearch.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"
#include "Language/Server/ServerLaunch.h"

#include "Core/FileSystem/FileHold.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"
#include "Core/Jobs/Mailbox.h"

#include <optional>

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstddef>

namespace Graphite {

	std::chrono::steady_clock::duration RestartBackoff(std::size_t restarts);

	inline constexpr std::size_t kMaximumRestarts = 3;

	inline constexpr std::chrono::minutes kStableAfter{2};

	bool MayRestart(std::size_t restarts);

	bool RestartBudgetRefilled(std::optional<std::chrono::steady_clock::time_point> upSince,
							   std::chrono::steady_clock::time_point now);

	inline constexpr std::chrono::minutes kIdleShutdown{2};

	class LanguageServices
	{
	public:
		explicit LanguageServices(std::filesystem::path fallbackRoot);

		void SetFallbackRoot(std::filesystem::path root);

		void NoteNamed(const std::filesystem::path& named, const std::filesystem::path& by, LanguageId language);

		~LanguageServices();

		LanguageServices(const LanguageServices&) = delete;
		LanguageServices& operator=(const LanguageServices&) = delete;

		void EnsureStarted(const std::filesystem::path& path, LanguageId language);

		void WarmUp(std::filesystem::path root, bool respectGitIgnore);

		using ChoiceProvider = std::function<ServerChoice(LanguageId language)>;

		void SetChoiceProvider(ChoiceProvider provider);

		void Reconfigure(LanguageId language);

		using RootPermission = std::function<bool(const std::filesystem::path& root)>;

		void SetRootPermission(RootPermission permission);

		using Clock = std::function<std::chrono::steady_clock::time_point()>;

		void SetClock(Clock clock);

		using ClientFactory = std::function<Core::Scope<LspClient>(const ServerLaunch& launch, const std::filesystem::path& root)>;

		void SetClientFactory(ClientFactory factory);

		void SetServersDirectory(std::filesystem::path directory);

		using DotnetLocator = std::function<std::vector<DotnetInstallation>()>;

		void SetDotnetLocator(DotnetLocator locator);

		const std::vector<DotnetInstallation>& DotnetInstallations() const;

		using UninstallingQuery = std::function<bool(std::string_view serverId)>;

		void SetUninstallingQuery(UninstallingQuery query);

		void SetLogDirectory(std::filesystem::path directory);

		void SetTemporaryFilesDirectory(std::filesystem::path directory);

		void SetSelectedSolution(const std::filesystem::path& solution);

		bool IsRunningFor(const std::filesystem::path& path, LanguageId language) const;

		void Revalidate();

		ServerStatus StatusFor(LanguageId language) const;

		bool HasChosenServer(LanguageId language) const;

		const ServerDefinition* ChosenServer(LanguageId language) const;

		std::optional<ServerCommand> FoundCommandFor(LanguageId language) const;

		const ServerChoice& ChoiceFor(LanguageId language) const;

		bool OffersSwitchSourceHeader(LanguageId language) const;

		ServerStatus StatusFor(const std::filesystem::path& path, LanguageId language);

		std::optional<ServerReport> ReportFor(const std::filesystem::path& path, LanguageId language) const;

		std::optional<ServerStatus> OverallStatus() const;

		void Poll();

		enum class Ticket : std::uint64_t
		{
		};

		Ticket AskDefinition(const std::filesystem::path& path, LanguageId language, const FilePosition& where);
		Ticket AskReferences(const std::filesystem::path& path, LanguageId language, const FilePosition& where);
		Ticket AskHover(const std::filesystem::path& path, LanguageId language, const FilePosition& where);
		Ticket AskSwitchSourceHeader(const std::filesystem::path& path, LanguageId language);
		Ticket AskSymbols(const std::filesystem::path& path, LanguageId language);
		Ticket AskSemanticTokens(const std::filesystem::path& path, LanguageId language);

		struct Answer : ServerReply
		{
			Ticket For{};
		};

		// WARNING: LspClient::Drain consumes what it returns, so only Poll may drain a client and only
		// TakeAnswers, TakeDiagnostics, TakeMessages and TakeRecolour may hand what it drained out.
		void TakeAnswers(std::vector<Answer>& answers);

		void TakeDiagnostics(DiagnosticsUpdate& update);

		void TakeMessages(std::vector<ServerMessage>& messages);

		void TakeRecolour(std::vector<std::filesystem::path>& paths);

		void Cancel(Ticket ticket);

		void OpenDocument(const std::filesystem::path& path, LanguageId language, std::string text,
						  DocumentRole role = DocumentRole::Consulted);
		void ChangeDocument(const std::filesystem::path& path, LanguageId language, std::string text);

		void SaveDocument(const std::filesystem::path& path, LanguageId language);

		void CloseDocument(const std::filesystem::path& path, LanguageId language);

		bool Holds(const std::filesystem::path& path, LanguageId language);

	private:
		struct ServerKey
		{
			LanguageId Language = LanguageId::PlainText;
			std::filesystem::path Root;

			friend bool operator<(const ServerKey& earlier, const ServerKey& later)
			{
				if (earlier.Language != later.Language)
				{
					return earlier.Language < later.Language;
				}

				return earlier.Root < later.Root;
			}
		};

		using KeyByPath = std::map<std::filesystem::path, ServerKey>;
		using RootCacheMap = std::map<std::pair<LanguageId, std::filesystem::path>, std::filesystem::path>;

		struct ManagedServer
		{
			const ServerDefinition* Definition = nullptr;
			ServerOverrides Overrides;
			std::filesystem::path Root;
			ServerCommand Command;

			std::optional<std::vector<std::filesystem::path>> Projects;

			std::uint64_t ProjectSearch = 0;

			std::optional<Core::FileHold> Hold;
			Core::Scope<LspClient> Client;
			ServerStatus Status;
			std::size_t Restarts = 0;

			std::optional<std::chrono::steady_clock::time_point> UpSince;

			bool RestartPending = false;
			std::chrono::steady_clock::time_point RestartAt{};

			struct SentDocument
			{
				std::string Text;
				DocumentVersion Version;
				DocumentRole Role = DocumentRole::Consulted;
			};

			std::map<std::filesystem::path, SentDocument> OpenDocuments;

			using PendingTickets = std::unordered_map<RequestId, Ticket>;

			PendingTickets Pending;

			std::optional<std::chrono::steady_clock::time_point> IdleSince;

			std::deque<ServerEvent> History;
			std::optional<std::chrono::steady_clock::time_point> StateSince;
		};

		using ServerMap = std::map<ServerKey, ManagedServer>;

		ManagedServer* FindManagedServer(const ServerKey& key);

		static const ServerKey* KeyOf(const KeyByPath& keys, const std::filesystem::path& path, LanguageId language);

		const ServerDefinition* Resolve(const std::filesystem::path& path, LanguageId language, ServerKey& key) const;

		const ServerDefinition* ServerFor(LanguageId language) const;

		bool FollowsSelectedSolution(LanguageId language) const;
		void WarmOpenFolderAgain();
		bool RunsServersOf(LanguageId language) const;

		struct HeldDocument
		{
			std::filesystem::path Path;
			std::string Text;
			DocumentRole Role = DocumentRole::Consulted;
		};

		struct ParkedDocument
		{
			LanguageId Language = LanguageId::PlainText;
			std::string Text;
			DocumentRole Role = DocumentRole::Consulted;
		};

		using ParkedMap = std::map<std::filesystem::path, ParkedDocument>;

		ParkedMap m_Parked;

		ParkedDocument* FindParkedDocument(const std::filesystem::path& path, LanguageId language);

		std::vector<HeldDocument> TakeParkedDocumentsOf(LanguageId language);

		struct WarmUpRequest
		{
			std::filesystem::path Root;
			bool RespectGitIgnore = false;
		};

		std::vector<HeldDocument> ReleaseServersOf(LanguageId language);

		bool IsWorkspaceRoot(const std::filesystem::path& root) const;
		void ForgetRootsOf(LanguageId language);

		ServerStatus OffStatus(LanguageId language) const;

		bool WarmUpNeedsOpen(LanguageId language) const;

		std::filesystem::path FallbackFor(const std::filesystem::path& path) const;

		void NoteNamedByKey(const std::filesystem::path& named, const ServerKey& key);

		void StartServer(ManagedServer& server, const ServerKey& key);

		void RestartServer(const ServerKey& key, ManagedServer& server);

		std::chrono::steady_clock::time_point Now() const;

		static void NoteEvent(ManagedServer& server, std::string text, std::optional<std::chrono::seconds> took = std::nullopt);
		void NoteStateEntered(ManagedServer& server, std::string text, std::optional<std::chrono::seconds> took = std::nullopt);
		void NoteStateChange(ManagedServer& server, const ServerStatus& next);

		void ReapIdle(std::chrono::steady_clock::time_point now);

		Clock m_Clock;

		ClientFactory m_ClientFactory;

		Core::Scope<LspClient> MakeClient(const ManagedServer& server, const ServerKey& key) const;

		void Retire(Core::Scope<LspClient> client);

		bool Permits(const std::filesystem::path& root) const;
		void MarkRestricted(ManagedServer& server);
		bool NeedsProjectSearch(const ManagedServer& server, const ServerKey& key) const;
		void BeginProjectSearch(ManagedServer& server, const ServerKey& key);
		void StartSearchedServers();
		void Restrict(const ServerKey& key, ManagedServer& server);
		void ForgetNamedBy(const ServerKey& key);
		void ReplayOpenDocuments(ManagedServer& server);

		RootPermission m_RootPermission;

		Ticket IssueTicket();

		void Fail(Ticket ticket);

		Ticket AskVia(const std::filesystem::path& path, LanguageId language,
					  const std::function<RequestId(LspClient&)>& ask);

		struct WarmFinding
		{
			LanguageId Language = LanguageId::PlainText;
			std::filesystem::path Path;
			std::string Text;
		};

		using WarmFindings = Core::Mailbox<WarmFinding>;

		static void RunWarmUpWalk(const Core::Ref<WarmFindings>& warm, const Core::CancellationToken& token,
								  const std::filesystem::path& root, const std::filesystem::path& selectedSolution,
								  bool respectGitIgnore, const std::vector<LanguageId>& wanted);

		Core::Ref<WarmFindings> m_Warm = Core::CreateRef<WarmFindings>();
		Core::CancellationToken m_WarmToken;

		struct FoundProjects
		{
			ServerKey Server;
			std::uint64_t Search = 0;
			ProjectsBelow Projects;
		};

		Core::Ref<Core::Mailbox<FoundProjects>> m_ProjectSearches = Core::CreateRef<Core::Mailbox<FoundProjects>>();
		std::uint64_t m_NextProjectSearch = 1;
		Core::CancellationToken m_ProjectSearchToken;

		std::filesystem::path m_FallbackRoot;
		std::optional<WarmUpRequest> m_LastWarmUp;
		ChoiceProvider m_ChoiceProvider;
		mutable std::map<LanguageId, ServerChoice> m_Choices;

		ServerCommandPlaces CommandPlaces() const;

		std::filesystem::path m_ServersDirectory;
		DotnetLocator m_DotnetLocator;
		UninstallingQuery m_Uninstalling;
		mutable std::optional<std::vector<DotnetInstallation>> m_Dotnet;
		std::filesystem::path m_LogDirectory;
		std::optional<ClaimedTemporaryDirectory> m_TemporaryFiles;
		std::filesystem::path m_SelectedSolution;

		ServerMap m_Servers;

		mutable RootCacheMap m_RootCache;

		KeyByPath m_DocumentKeys;

		KeyByPath m_NamedBy;

		std::uint64_t m_NextTicket = 1;
		std::vector<Answer> m_Answers;
		DiagnosticsUpdate m_Diagnostics;
		std::vector<std::filesystem::path> m_Recolour;
		std::vector<ServerMessage> m_Messages;

		// WARNING: Declared last so it is destroyed first and joins its worker before anything the worker
		// touches goes away.
		Core::JobQueue m_Jobs;
	};
}

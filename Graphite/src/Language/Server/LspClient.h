#pragma once

#include "Core/Base/Base.h"

#include "Core/Platform/ChildProcess.h"
#include "Language/Api/HoverInfo.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/JsonRpc.h"
#include "Language/Server/LspProtocol.h"
#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/ServerLaunch.h"

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

	enum class ReplyFailure : std::uint8_t
	{
		ServerError,
		TimedOut,
		ServerUnavailable
	};

	// What a server answered to one request, whichever kind of request it was.
	struct ServerReply
	{
		std::vector<Location> Locations;
		std::vector<Declaration> Declarations;
		std::vector<SemanticToken> SemanticTokens;
		std::string Text;
		HoverInfo Hover;
		bool Failed = false;

		// WARNING: Both are read only when Failed is set. ServerAtFailure is the server's status as the request
		// failed, which a restart in the same poll has already replaced by the time anyone reads the answer.
		ReplyFailure FailedBecause = ReplyFailure::ServerError;
		ServerStatus ServerAtFailure;
	};

	class LspClient
	{
	public:
		LspClient(const ServerLaunch& launch, std::filesystem::path root);

		struct WithoutServer
		{
		};

		LspClient(const ServerLaunch& launch, std::filesystem::path root, WithoutServer);

		~LspClient();

		LspClient(const LspClient&) = delete;
		LspClient& operator=(const LspClient&) = delete;

		DocumentVersion OpenDocument(const std::filesystem::path& path, const std::string& text,
									 DocumentRole role = DocumentRole::Consulted);
		// WARNING: Returns a default-constructed version, meaning nothing was sent, for a document the server was never told about.
		DocumentVersion ChangeDocument(const std::filesystem::path& path, const std::string& text);
		void SaveDocument(const std::filesystem::path& path);
		void CloseDocument(const std::filesystem::path& path);

		RequestId AskDefinition(const FilePosition& where);
		RequestId AskReferences(const FilePosition& where);

		RequestId AskHover(const FilePosition& where);

		RequestId AskSwitchSourceHeader(const std::filesystem::path& path);
		RequestId AskSymbols(const std::filesystem::path& path);

		// WARNING: Returns 0, which no request ever uses, when the server declared no semantic tokens: there is no legend to read a reply through.
		RequestId AskSemanticTokens(const std::filesystem::path& path);

		struct Answer : ServerReply
		{
			RequestId Id = 0;
		};

		static constexpr std::chrono::seconds kHandshakeTimeout{30};

		static constexpr std::chrono::seconds kNavigationTimeLimit{30};
		static constexpr std::chrono::seconds kReferencesTimeLimit{120};
		static constexpr std::chrono::seconds kAnalysisTimeLimit{60};

		void ExpireHandshakeIfStalled(std::chrono::steady_clock::time_point now);

		void ExpireOverdueRequests(std::chrono::steady_clock::time_point now);

		std::size_t QueuedCount() const;

		struct Drained
		{
			std::vector<Answer> Answers;
			ServerStatus Status;
			std::vector<PublishedDiagnostics> Diagnostics;
			std::vector<ServerMessage> Messages;

			std::vector<std::filesystem::path> Recolour;

			std::vector<DocumentVersionNote> Unanswered;
		};

		// WARNING: UI thread only.
		void Drain(Drained& drained);

		void Cancel(RequestId id);

		static constexpr std::chrono::seconds kShutdownGrace{2};

		// WARNING: Blocks for up to the grace, so it must not run on the UI thread.
		void ShutDown(std::chrono::milliseconds grace);

		void Deliver(const std::string& body);

		std::vector<std::string> TakeSent();

	private:
		enum class Question : std::uint8_t
		{
			Definition,
			References,
			Symbols,
			SemanticTokens,
			SwitchSourceHeader,
			Hover,
			Diagnostics
		};

		struct Document
		{
			int Version = 0;

			// WARNING: Paths compare by spelling, drive letter included, so anything reported about a document must carry this path rather than one read back from a URI.
			std::filesystem::path Path;

			DocumentRole Role = DocumentRole::Consulted;

			// WARNING: Sent trails Version while a message's JSON is built outside the lock. Pulls and unversioned publications name Sent because the server has not been sent the newer text.
			int Sent = 0;

			RequestId Pull = 0;
			bool PullAgain = false;

			bool Retriggered = false;
		};

		struct Asked
		{
			Question Kind = Question::Definition;

			std::optional<FilePosition> From;

			std::string Uri;
			DocumentVersion Version;

			// WARNING: Empty while the request waits in the pre-handshake queue: the time limit counts from when the
			// request reaches the server, not from when it was asked.
			std::optional<std::chrono::steady_clock::time_point> Deadline;
		};

		using DocumentTable = std::unordered_map<std::string, Document>;
		using PendingTable = std::unordered_map<RequestId, Asked>;

		// WARNING: Lock order is ProtocolMutex first, then StateMutex or OutboundMutex, never the reverse, and those two are never held together. No lock is held across a write to the pipe.
		struct Shared
		{
			std::mutex StateMutex;
			std::vector<Answer> Answers;
			ServerStatus Status;
			std::vector<PublishedDiagnostics> Diagnostics;
			std::vector<ServerMessage> Messages;
			std::vector<std::filesystem::path> Recolour;
			std::vector<DocumentVersionNote> Unanswered;

			struct RunningProgress
			{
				nlohmann::json Token;
				std::string Title;
				std::string Message;
			};

			std::vector<RunningProgress> Progress;

			std::optional<std::string> ProjectsLoading;

			std::mutex ProtocolMutex;
			JsonRpc Rpc;
			PendingTable Pending;
			DocumentTable Documents;

			// WARNING: Kept after a document closes so a reopen continues the numbering. Restarting at 1 would let a publication in flight for the old version read as answering the new one.
			std::unordered_map<std::string, int> LastVersions;
			std::vector<std::string> Queued;
			RequestId InitializeId = 0;
			bool Initialized = false;

			std::vector<SemanticKind> SemanticLegend;

			bool PullsDiagnostics = false;

			// WARNING: A handshake that will never finish must drop messages rather than queue them, or every keystroke appends a whole copy of the file.
			bool HandshakeFailed = false;

			std::chrono::steady_clock::time_point HandshakeDeadline{};

			bool ReaderStopped = false;

			std::mutex OutboundMutex;
			std::condition_variable Outbound;
			std::vector<std::string> ToSend;

			bool WriterStopped = false;

			std::atomic<bool> Stopping{false};

			Core::Ref<Core::ChildProcess> Process;
			std::string ProtocolLanguageId;
			const ServerAdapter* Adapter = nullptr;
			AdapterContext Context;
			std::optional<ProjectProblem> Problem;
		};

		RequestId AskLocked(std::string_view method, const nlohmann::json& params, Question question,
							std::optional<FilePosition> from = std::nullopt);

		static std::chrono::seconds TimeLimitOf(Question question);

		// WARNING: ProtocolMutex must be held by the caller.
		static std::optional<std::chrono::steady_clock::time_point> DeadlineFromNowLocked(const Shared& shared, Question question);

		static void StartWaitingDeadlinesLocked(Shared& shared);

		static bool IsOverdue(const Asked& asked, std::chrono::steady_clock::time_point now);

		static Answer FailedAnswer(RequestId id, ReplyFailure reason);

		void BeginHandshake(const std::filesystem::path& root);

		bool RequestShutdown();

		void Adopt(const ServerLaunch& launch, const std::filesystem::path& root);

		// WARNING: StateMutex must be held by the caller.
		static void Settle(Shared& shared);

		static void RunReader(Core::Ref<Shared> shared);
		static void RunWriter(Core::Ref<Shared> shared);

		static void HandleBody(Shared& shared, const std::string& body);

		static void AnswerServerRequestLocked(Shared& shared, const IncomingMessage& request);
		static void CompleteHandshakeLocked(Shared& shared, const IncomingMessage& reply);

		static void HandleNotification(Shared& shared, const IncomingMessage& notification);
		static void HandleReply(Shared& shared, const IncomingMessage& reply);

		static void PublishDiagnostics(Shared& shared, const nlohmann::json& params);

		static Document* FindDocument(Shared& shared, const std::string& uri);
		static const Document* FindDocument(const Shared& shared, const std::string& uri);

		static bool WriterHasWork(const Shared& shared);

		static void MarkSentLocked(Shared& shared, const std::string& uri, int version);

		static void PullLocked(Shared& shared, const std::string& uri);

		static void PullShownLocked(Shared& shared);

		static void ReceivePull(Shared& shared, const Asked& asked, const IncomingMessage& reply);

		// WARNING: ProtocolMutex must be held by the caller.
		static std::optional<DocumentVersionNote> GiveUpPullLocked(Shared& shared, const Asked& asked);

		static void NameOpenDocumentsAsOpenedLocked(const Shared& shared, std::vector<Location>& locations);

		static void RecordServerMessage(Shared& shared, const nlohmann::json& params);
		static void ApplyProgress(Shared& shared, const nlohmann::json& params);
		static std::string ActivityOf(const std::vector<Shared::RunningProgress>& running);
		static Shared::RunningProgress* FindProgress(std::vector<Shared::RunningProgress>& running, const nlohmann::json& token);
		static bool IsProgressOf(const Shared::RunningProgress& progress, const nlohmann::json& token);

		static Answer ReadAnswer(const Asked& asked, const IncomingMessage& reply,
								 const std::vector<SemanticKind>& legend);

		static void SendLocked(Shared& shared, std::string bytes);
		static void SendNowLocked(Shared& shared, std::string bytes);

		static void StopOutbound(Shared& shared);

		void StopThreads();

		Core::Ref<Shared> m_Shared = Core::CreateRef<Shared>();

		std::thread m_Reader;
		std::thread m_Writer;
	};
}

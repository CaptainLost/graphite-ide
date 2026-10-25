#include "Language/Server/LspClient.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Language/Server/MessageStream.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {
	namespace {
		std::string StringField(const nlohmann::json& object, const char* name)
		{
			const nlohmann::json::const_iterator field = object.find(name);

			return field != object.end() && field->is_string() ? field->get<std::string>() : std::string{};
		}

		std::string SectionOf(const nlohmann::json& item)
		{
			if (!item.is_object())
			{
				return {};
			}

			const nlohmann::json::const_iterator section = item.find("section");

			return section != item.end() && section->is_string() ? section->get<std::string>() : std::string{};
		}

		std::vector<std::string> RequestedSections(const nlohmann::json& params)
		{
			std::vector<std::string> sections;

			if (params.is_object())
			{
				const nlohmann::json::const_iterator items = params.find("items");

				if (items != params.end() && items->is_array())
				{
					for (const nlohmann::json& item : *items)
					{
						sections.push_back(SectionOf(item));
					}
				}
			}

			return sections;
		}

		nlohmann::json ConfigurationReply(const ServerAdapter& adapter, const AdapterContext& context, const nlohmann::json& params)
		{
			nlohmann::json result = nlohmann::json::array();
			const std::vector<std::string> sections = RequestedSections(params);

			if (sections.empty() && adapter.AnswersEmptyConfigurationWithOneEntry())
			{
				result.push_back(adapter.WorkspaceConfiguration(context, ""));
				return result;
			}

			for (const std::string& section : sections)
			{
				result.push_back(adapter.WorkspaceConfiguration(context, section));
			}

			return result;
		}

		constexpr int kMethodNotFound = -32601;

		constexpr int kShowMessageError = 1;
		constexpr int kShowMessageWarning = 2;

		constexpr int kNoPercent = -1;
		constexpr std::size_t kReadBufferSize = 8192;
		constexpr RequestId kNoRequest = 0;
		constexpr int kNothingSent = 0;
		constexpr bool kIncludeDeclaration = true;
		constexpr bool kStopRequested = true;

		// WARNING: Null to workDoneProgress/create means "report under that token" and null to showMessageRequest means no action was chosen. Refusing a registration stops some servers.
		constexpr std::array<std::string_view, 4> kRequestsAcknowledgedWithNull{
			"window/workDoneProgress/create", "client/registerCapability", "client/unregisterCapability", "window/showMessageRequest"};

		bool IsAcknowledgedWithNull(std::string_view method)
		{
			for (const std::string_view acknowledged : kRequestsAcknowledgedWithNull)
			{
				if (acknowledged == method)
				{
					return true;
				}
			}

			return false;
		}

		bool AsksToBeRetriggered(const nlohmann::json& error)
		{
			if (!error.is_object())
			{
				return false;
			}

			const nlohmann::json::const_iterator data = error.find("data");

			if (data == error.end() || !data->is_object())
			{
				return false;
			}

			const nlohmann::json::const_iterator retrigger = data->find("retriggerRequest");

			return retrigger != data->end() && retrigger->is_boolean() && retrigger->get<bool>();
		}

		std::optional<ServerMessageKind> ShownMessageKind(const nlohmann::json& params)
		{
			const nlohmann::json::const_iterator type = params.find("type");

			if (type == params.end() || !type->is_number_integer())
			{
				return std::nullopt;
			}

			const int messageType = type->get<int>();

			if (messageType == kShowMessageError)
			{
				return ServerMessageKind::Error;
			}

			if (messageType == kShowMessageWarning)
			{
				return ServerMessageKind::Warning;
			}

			return std::nullopt;
		}

		nlohmann::json TextDocumentParams(const std::string& uri)
		{
			nlohmann::json params;
			params["textDocument"]["uri"] = uri;

			return params;
		}

		nlohmann::json PositionParams(const FilePosition& where)
		{
			nlohmann::json params = TextDocumentParams(ToFileUri(where.Path));
			params["position"]["line"] = where.Line;
			params["position"]["character"] = where.Column.Value;

			return params;
		}

		bool IsPublishedFor(const PublishedDiagnostics& published, const std::string& uri)
		{
			return ToFileUri(published.Path) == uri;
		}

		bool IsPositionAskedFrom(const Location& location, const FilePosition& from)
		{
			return location.Line == from.Line && location.Column == from.Column && location.Path == from.Path;
		}

		nlohmann::json InitializeParams(const std::filesystem::path& root, const nlohmann::json& initializationOptions)
		{
			nlohmann::json capabilities;

			// WARNING: Without declaring workDoneProgress clangd sends no $/progress, and the status bar has no percentage to show.
			capabilities["window"]["workDoneProgress"] = true;
			capabilities["workspace"]["configuration"] = true;

			capabilities["textDocument"]["synchronization"]["didSave"] = true;
			capabilities["textDocument"]["synchronization"]["dynamicRegistration"] = false;

			// WARNING: linkSupport stays off because a LocationLink holds several ranges with different meanings and only one is where a reader wants to land.
			capabilities["textDocument"]["definition"]["linkSupport"] = false;

			capabilities["textDocument"]["semanticTokens"]["requests"]["full"] = true;
			capabilities["textDocument"]["semanticTokens"]["formats"] = nlohmann::json::array({"relative"});
			capabilities["textDocument"]["semanticTokens"]["tokenTypes"] = nlohmann::json::array(
				{"namespace", "type", "class", "enum", "interface", "struct", "typeParameter",
				 "concept", "parameter", "variable", "property", "enumMember", "function",
				 "method", "macro", "event", "field", "fieldName", "constant", "constantName",
				 "extensionMethod", "extensionMethodName", "recordClass", "recordStruct",
				 "delegate", "delegateName"});
			capabilities["textDocument"]["semanticTokens"]["tokenModifiers"] = nlohmann::json::array();

			// WARNING: Roslyn answers only pulls and csharp-ls stops publishing once pulls are declared. Dynamic registration is refused so the offer arrives in the handshake.
			capabilities["textDocument"]["diagnostic"]["dynamicRegistration"] = false;
			capabilities["textDocument"]["diagnostic"]["relatedDocumentSupport"] = false;

			capabilities["workspace"]["diagnostics"]["refreshSupport"] = true;
			// WARNING: Markdown comes first because clangd's markdown reply is the one ParseClangdHover reads into fields.
			capabilities["textDocument"]["hover"]["contentFormat"] =
				nlohmann::json::array({"markdown", "plaintext"});
			capabilities["textDocument"]["references"]["dynamicRegistration"] = false;
			capabilities["textDocument"]["documentSymbol"]["hierarchicalDocumentSymbolSupport"] =
				true;

			nlohmann::json params;
			params["processId"] = nullptr;
			params["rootUri"] = ToFileUri(root);
			params["rootPath"] = Core::ToUtf8(root);
			params["capabilities"] = std::move(capabilities);

			if (!initializationOptions.is_null())
			{
				params["initializationOptions"] = initializationOptions;
			}

			return params;
		}

		std::string DescribeError(const nlohmann::json& error)
		{
			if (error.is_object())
			{
				const nlohmann::json::const_iterator message = error.find("message");

				if (message != error.end() && message->is_string())
				{
					return message->get<std::string>();
				}
			}

			return "the language server refused to start";
		}
	}

	LspClient::Document* LspClient::FindDocument(Shared& shared, const std::string& uri)
	{
		return shared.Documents.contains(uri) ? &shared.Documents.at(uri) : nullptr;
	}

	const LspClient::Document* LspClient::FindDocument(const Shared& shared, const std::string& uri)
	{
		return shared.Documents.contains(uri) ? &shared.Documents.at(uri) : nullptr;
	}

	bool LspClient::WriterHasWork(const Shared& shared)
	{
		return !shared.ToSend.empty() || shared.Stopping.load() || shared.WriterStopped;
	}

	void LspClient::SendNowLocked(Shared& shared, std::string bytes)
	{
		// WARNING: The message is queued, never written here. A synchronous write under ProtocolMutex blocks on a full pipe, and the caller may be the reader, which must never stop.
		{
			const std::lock_guard<std::mutex> outbound(shared.OutboundMutex);

			if (shared.WriterStopped)
			{
				return;
			}

			shared.ToSend.push_back(std::move(bytes));
		}

		shared.Outbound.notify_one();
	}

	void LspClient::SendLocked(Shared& shared, std::string bytes)
	{
		if (!shared.Initialized)
		{
			if (shared.HandshakeFailed)
			{
				shared.Queued.clear();
				return;
			}

			{
				const std::lock_guard<std::mutex> outbound(shared.OutboundMutex);

				if (shared.WriterStopped)
				{
					shared.Queued.clear();
					return;
				}
			}

			shared.Queued.push_back(std::move(bytes));
			return;
		}

		SendNowLocked(shared, std::move(bytes));
	}

	void LspClient::StopOutbound(Shared& shared)
	{
		{
			const std::lock_guard<std::mutex> outbound(shared.OutboundMutex);

			shared.WriterStopped = true;
			shared.ToSend.clear();
		}

		shared.Outbound.notify_all();
	}

	void LspClient::BeginHandshake(const std::filesystem::path& root)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		// WARNING: The id is recorded under the mutex the reader takes, before the request is queued, so the reply never reaches a reader that does not know it.
		auto [id, bytes] = m_Shared->Rpc.Request("initialize", InitializeParams(root, m_Shared->Adapter->InitializationOptions(m_Shared->Context)));

		m_Shared->InitializeId = id;
		m_Shared->HandshakeDeadline = std::chrono::steady_clock::now() + kHandshakeTimeout;

		SendNowLocked(*m_Shared, std::move(bytes));
	}

	void LspClient::Adopt(const ServerLaunch& launch, const std::filesystem::path& root)
	{
		CORE_ASSERT(launch.Server != nullptr, "a launch needs a server to adopt");
		CORE_ASSERT(launch.Adapter != nullptr, "a launch needs an adapter to adopt");

		m_Shared->ProtocolLanguageId = std::string(launch.Server->ProtocolLanguageId);
		m_Shared->Adapter = launch.Adapter;
		m_Shared->Context = AdapterContext{.Root = root,
										   .InitializationOptions = launch.InitializationOptions,
										   .Settings = launch.Settings,
										   .Solution = launch.Solution,
										   .Projects = launch.Projects};

		m_Shared->Problem = launch.Adapter->ProjectProblemAt(m_Shared->Context);

		m_Shared->ProjectsLoading = launch.Adapter->LoadsProjectsFrom(m_Shared->Context);

		m_Shared->Status.Name = std::string(launch.Server->DisplayName);
		m_Shared->Status.State = ServerState::Starting;
		m_Shared->Status.Detail = launch.Description.empty() ? Core::ToUtf8(launch.Binary) : launch.Description;
	}

	void LspClient::Settle(Shared& shared)
	{
		if (shared.ProjectsLoading)
		{
			shared.Status.State = ServerState::Indexing;
			shared.Status.Loading = *shared.ProjectsLoading;
			return;
		}

		shared.Status.Loading.clear();

		if (shared.Problem)
		{
			shared.Status.State = ServerState::ProjectIncomplete;
			shared.Status.Problem = *shared.Problem;
			return;
		}

		shared.Status.State = ServerState::Ready;
	}

	LspClient::LspClient(const ServerLaunch& launch, std::filesystem::path root)
	{
		Adopt(launch, root);

		Core::Result<Core::Scope<Core::ChildProcess>> started = Core::ChildProcess::Start(launch.Binary, launch.Arguments, root, launch.Environment);

		if (!started)
		{
			m_Shared->Status.State = ServerState::Failed;
			m_Shared->Status.Detail = started.GetError().Message;

			// WARNING: With no process there is no reader to fail questions and no writer to empty the outbound queue, so both are stopped here.
			{
				const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);
				m_Shared->ReaderStopped = true;

				m_Shared->HandshakeFailed = true;
			}

			StopOutbound(*m_Shared);

			return;
		}

		m_Shared->Process = Core::Ref<Core::ChildProcess>(std::move(started.GetValue()));

		BeginHandshake(root);

		// WARNING: Starting a thread can throw and a joinable thread destroyed unjoined ends the process. The destructor does not run for a throwing constructor, so StopThreads runs here.
		try
		{
			m_Reader = std::thread(&LspClient::RunReader, m_Shared);
			m_Writer = std::thread(&LspClient::RunWriter, m_Shared);
		}
		catch (...)
		{
			StopThreads();
			throw;
		}
	}

	LspClient::LspClient(const ServerLaunch& launch, std::filesystem::path root, WithoutServer)
	{
		Adopt(launch, root);

		BeginHandshake(root);
	}

	LspClient::~LspClient()
	{
		StopThreads();
	}

	void LspClient::StopThreads()
	{
		m_Shared->Stopping.store(kStopRequested);

		// WARNING: Terminating the child releases the reader blocked on the pipe and the writer blocked writing to it. Waiting for the server to leave here would hold the UI thread.
		if (m_Shared->Process)
		{
			m_Shared->Process->Terminate();
		}

		m_Shared->Outbound.notify_all();

		if (m_Writer.joinable())
		{
			m_Writer.join();
		}

		if (m_Reader.joinable())
		{
			m_Reader.join();
		}
	}

	DocumentVersion LspClient::OpenDocument(const std::filesystem::path& path,
											const std::string& text, DocumentRole role)
	{
		const std::string uri = ToFileUri(path);

		int version = 0;

		{
			const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

			int& lastVersion = m_Shared->LastVersions[uri];
			++lastVersion;

			Document& document = m_Shared->Documents[uri];
			document.Version = lastVersion;
			document.Path = path;
			document.Role = role;
			version = lastVersion;
		}

		// WARNING: Built outside ProtocolMutex because copying and escaping the whole file would stall the reader, which takes that mutex for every message.
		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["languageId"] = m_Shared->ProtocolLanguageId;
		params["textDocument"]["version"] = version;
		params["textDocument"]["text"] = text;

		std::string bytes = JsonRpc::Notification("textDocument/didOpen", params);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);
		SendLocked(*m_Shared, std::move(bytes));
		MarkSentLocked(*m_Shared, uri, version);
		PullLocked(*m_Shared, uri);

		return DocumentVersion{version};
	}

	DocumentVersion LspClient::ChangeDocument(const std::filesystem::path& path,
											  const std::string& text)
	{
		const std::string uri = ToFileUri(path);

		int version = 0;

		{
			const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

			Document* document = FindDocument(*m_Shared, uri);

			if (document == nullptr)
			{
				return DocumentVersion{};
			}

			version = ++document->Version;
		}

		// WARNING: Full text rather than a range, because a range computed slightly wrong silently corrupts the server's copy.
		nlohmann::json change;
		change["text"] = text;

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["version"] = version;
		params["contentChanges"] = nlohmann::json::array({std::move(change)});

		std::string bytes = JsonRpc::Notification("textDocument/didChange", params);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);
		SendLocked(*m_Shared, std::move(bytes));
		MarkSentLocked(*m_Shared, uri, version);

		PullShownLocked(*m_Shared);

		return DocumentVersion{version};
	}

	void LspClient::SaveDocument(const std::filesystem::path& path)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		if (!m_Shared->Documents.contains(uri))
		{
			return;
		}

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didSave", TextDocumentParams(uri)));
	}

	void LspClient::CloseDocument(const std::filesystem::path& path)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		const Document* document = FindDocument(*m_Shared, uri);

		if (document == nullptr)
		{
			return;
		}

		const RequestId pullInFlight = document->Pull;

		if (pullInFlight != kNoRequest)
		{
			m_Shared->Pending.erase(pullInFlight);
			SendLocked(*m_Shared, m_Shared->Rpc.Cancel(pullInFlight));
		}

		m_Shared->Documents.erase(uri);

		{
			const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

			std::erase_if(m_Shared->Diagnostics, [&uri](const PublishedDiagnostics& published) { return IsPublishedFor(published, uri); });
		}

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didClose", TextDocumentParams(uri)));
	}

	LspClient::Answer LspClient::FailedAnswer(RequestId id, ReplyFailure reason)
	{
		Answer answer;
		answer.Id = id;
		answer.Failed = true;
		answer.FailedBecause = reason;

		return answer;
	}

	std::chrono::seconds LspClient::TimeLimitOf(Question question)
	{
		switch (question)
		{
		case Question::Definition:
		case Question::Hover:
		case Question::SwitchSourceHeader:
			return kNavigationTimeLimit;

		case Question::References:
			return kReferencesTimeLimit;

		case Question::Symbols:
		case Question::SemanticTokens:
		case Question::Diagnostics:
			return kAnalysisTimeLimit;
		}

		return kAnalysisTimeLimit;
	}

	std::optional<std::chrono::steady_clock::time_point> LspClient::DeadlineFromNowLocked(const Shared& shared, Question question)
	{
		if (!shared.Initialized)
		{
			return std::nullopt;
		}

		return std::chrono::steady_clock::now() + TimeLimitOf(question);
	}

	void LspClient::StartWaitingDeadlinesLocked(Shared& shared)
	{
		for (auto& [requestId, asked] : shared.Pending)
		{
			if (!asked.Deadline)
			{
				asked.Deadline = DeadlineFromNowLocked(shared, asked.Kind);
			}
		}
	}

	bool LspClient::IsOverdue(const Asked& asked, std::chrono::steady_clock::time_point now)
	{
		return asked.Deadline.has_value() && now >= *asked.Deadline;
	}

	RequestId LspClient::AskLocked(std::string_view method, const nlohmann::json& params,
								   Question question, std::optional<FilePosition> from)
	{
		auto [id, bytes] = m_Shared->Rpc.Request(method, params);

		if (m_Shared->ReaderStopped)
		{
			m_Shared->Rpc.Forget(id);

			const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

			m_Shared->Answers.push_back(FailedAnswer(id, ReplyFailure::ServerUnavailable));

			return id;
		}

		m_Shared->Pending[id] = Asked{.Kind = question, .From = std::move(from), .Deadline = DeadlineFromNowLocked(*m_Shared, question)};
		SendLocked(*m_Shared, std::move(bytes));

		return id;
	}

	RequestId LspClient::AskDefinition(const FilePosition& where)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		return AskLocked("textDocument/definition", PositionParams(where), Question::Definition);
	}

	RequestId LspClient::AskHover(const FilePosition& where)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		return AskLocked("textDocument/hover", PositionParams(where), Question::Hover);
	}

	RequestId LspClient::AskSwitchSourceHeader(const std::filesystem::path& path)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["uri"] = ToFileUri(path);

		return AskLocked("textDocument/switchSourceHeader", params, Question::SwitchSourceHeader);
	}

	RequestId LspClient::AskReferences(const FilePosition& where)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params = PositionParams(where);
		params["context"]["includeDeclaration"] = kIncludeDeclaration;

		return AskLocked("textDocument/references", params, Question::References, where);
	}

	RequestId LspClient::AskSemanticTokens(const std::filesystem::path& path)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		if (m_Shared->SemanticLegend.empty())
		{
			return kNoRequest;
		}

		return AskLocked("textDocument/semanticTokens/full", TextDocumentParams(ToFileUri(path)), Question::SemanticTokens);
	}

	RequestId LspClient::AskSymbols(const std::filesystem::path& path)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		return AskLocked("textDocument/documentSymbol", TextDocumentParams(ToFileUri(path)), Question::Symbols);
	}

	void LspClient::Cancel(RequestId id)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		if (m_Shared->Pending.erase(id) == 0)
		{
			return;
		}

		SendLocked(*m_Shared, m_Shared->Rpc.Cancel(id));
	}

	bool LspClient::RequestShutdown()
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		auto [id, bytes] = m_Shared->Rpc.Request("shutdown", nullptr);
		m_Shared->Rpc.Forget(id);
		SendLocked(*m_Shared, std::move(bytes));

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("exit", nullptr));

		return m_Shared->Initialized;
	}

	void LspClient::ShutDown(std::chrono::milliseconds grace)
	{
		const bool serverWasAsked = RequestShutdown();

		// WARNING: The writer sends everything already queued, exit included, before it stops.
		m_Shared->Stopping.store(kStopRequested);
		m_Shared->Outbound.notify_all();

		if (serverWasAsked && m_Shared->Process && !m_Shared->Process->WaitForExit(grace))
		{
			const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

			APP_WARN("{} did not leave within {} ms of being asked to, so it was ended", m_Shared->Status.Name, grace.count());
		}

		StopThreads();
	}

	void LspClient::ExpireHandshakeIfStalled(std::chrono::steady_clock::time_point now)
	{
		{
			const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

			if (m_Shared->Initialized || m_Shared->HandshakeFailed ||
				now < m_Shared->HandshakeDeadline)
			{
				return;
			}

			m_Shared->HandshakeFailed = true;

			m_Shared->Queued.clear();
		}

		const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

		m_Shared->Status.State = ServerState::Failed;
		m_Shared->Status.Detail = "no answer to initialize within " +
								  std::to_string(kHandshakeTimeout.count()) + " seconds";
		m_Shared->Status.Percent = kNoPercent;
	}

	std::optional<DocumentVersionNote> LspClient::GiveUpPullLocked(Shared& shared, const Asked& asked)
	{
		Document* document = FindDocument(shared, asked.Uri);

		if (document == nullptr)
		{
			return std::nullopt;
		}

		document->Pull = kNoRequest;

		const DocumentVersionNote unanswered{.Path = document->Path, .Version = asked.Version};

		if (document->PullAgain)
		{
			PullLocked(shared, asked.Uri);
		}

		return unanswered;
	}

	void LspClient::ExpireOverdueRequests(std::chrono::steady_clock::time_point now)
	{
		std::vector<Answer> timedOut;
		std::vector<DocumentVersionNote> unanswered;

		{
			const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

			// WARNING: Collected first, because giving up a pull can send the next one, and inserting into the table
			// while walking it invalidates the walk.
			std::vector<RequestId> overdue;

			for (const auto& [requestId, asked] : m_Shared->Pending)
			{
				if (IsOverdue(asked, now))
				{
					overdue.push_back(requestId);
				}
			}

			for (const RequestId requestId : overdue)
			{
				PendingTable::node_type pending = m_Shared->Pending.extract(requestId);
				const Asked& asked = pending.mapped();

				SendLocked(*m_Shared, m_Shared->Rpc.Cancel(requestId));

				if (asked.Kind != Question::Diagnostics)
				{
					timedOut.push_back(FailedAnswer(requestId, ReplyFailure::TimedOut));
					continue;
				}

				if (std::optional<DocumentVersionNote> givenUp = GiveUpPullLocked(*m_Shared, asked))
				{
					unanswered.push_back(std::move(*givenUp));
				}
			}
		}

		if (timedOut.empty() && unanswered.empty())
		{
			return;
		}

		const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

		m_Shared->Answers.insert(m_Shared->Answers.end(), std::make_move_iterator(timedOut.begin()),
								 std::make_move_iterator(timedOut.end()));
		m_Shared->Unanswered.insert(m_Shared->Unanswered.end(), std::make_move_iterator(unanswered.begin()),
									std::make_move_iterator(unanswered.end()));
	}

	std::size_t LspClient::QueuedCount() const
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		return m_Shared->Queued.size();
	}

	void LspClient::Drain(Drained& drained)
	{
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		ExpireHandshakeIfStalled(now);
		ExpireOverdueRequests(now);

		const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

		drained.Unanswered.insert(drained.Unanswered.end(), std::make_move_iterator(m_Shared->Unanswered.begin()),
								  std::make_move_iterator(m_Shared->Unanswered.end()));

		m_Shared->Unanswered.clear();

		drained.Answers.insert(drained.Answers.end(),
							   std::make_move_iterator(m_Shared->Answers.begin()),
							   std::make_move_iterator(m_Shared->Answers.end()));

		m_Shared->Answers.clear();

		drained.Diagnostics.insert(drained.Diagnostics.end(),
								   std::make_move_iterator(m_Shared->Diagnostics.begin()),
								   std::make_move_iterator(m_Shared->Diagnostics.end()));

		m_Shared->Diagnostics.clear();

		drained.Messages.insert(drained.Messages.end(), std::make_move_iterator(m_Shared->Messages.begin()),
								std::make_move_iterator(m_Shared->Messages.end()));

		m_Shared->Messages.clear();

		drained.Recolour.insert(drained.Recolour.end(), std::make_move_iterator(m_Shared->Recolour.begin()),
								std::make_move_iterator(m_Shared->Recolour.end()));

		m_Shared->Recolour.clear();

		drained.Status = m_Shared->Status;
	}

	void LspClient::Deliver(const std::string& body)
	{
		HandleBody(*m_Shared, body);
	}

	std::vector<std::string> LspClient::TakeSent()
	{
		if (m_Shared->Process)
		{
			return {};
		}

		const std::lock_guard<std::mutex> outbound(m_Shared->OutboundMutex);

		std::vector<std::string> sent;
		sent.swap(m_Shared->ToSend);

		return sent;
	}

	void LspClient::RunWriter(Core::Ref<Shared> shared)
	{
		Core::ChildProcess& process = *shared->Process;

		for (;;)
		{
			std::vector<std::string> batch;

			{
				std::unique_lock<std::mutex> outbound(shared->OutboundMutex);

				shared->Outbound.wait(outbound, [&shared] { return WriterHasWork(*shared); });

				if (shared->WriterStopped)
				{
					return;
				}

				if (shared->ToSend.empty())
				{
					outbound.unlock();
					StopOutbound(*shared);

					return;
				}

				batch.swap(shared->ToSend);
			}

			for (const std::string& message : batch)
			{
				if (!process.Write(message))
				{
					StopOutbound(*shared);

					return;
				}
			}
		}
	}

	void LspClient::RunReader(Core::Ref<Shared> shared)
	{
		MessageStream stream;
		std::array<char, kReadBufferSize> buffer{};

		Core::ChildProcess& process = *shared->Process;

		for (;;)
		{
			const std::size_t bytesRead = process.Read(buffer);

			if (bytesRead == 0)
			{
				break;
			}

			stream.Append(std::string_view(buffer.data(), bytesRead));

			while (std::optional<std::string> body = stream.Take())
			{
				HandleBody(*shared, *body);
			}

			Core::Redraw::Request();
		}

		std::vector<RequestId> abandoned;

		{
			const std::lock_guard<std::mutex> protocol(shared->ProtocolMutex);

			for ([[maybe_unused]] const auto& [requestId, question] : shared->Pending)
			{
				abandoned.push_back(requestId);
			}

			shared->Pending.clear();

			// WARNING: Abandonment and ReaderStopped happen together under the mutex Ask holds, so a question is failed either here or by Ask, never neither.
			shared->ReaderStopped = true;
		}

		StopOutbound(*shared);

		const std::lock_guard<std::mutex> state(shared->StateMutex);

		for (const RequestId id : abandoned)
		{
			shared->Answers.push_back(FailedAnswer(id, ReplyFailure::ServerUnavailable));
		}

		if (!shared->Stopping.load())
		{
			shared->Status.State = ServerState::Failed;
			shared->Status.Detail = "the language server stopped";
			shared->Status.Percent = kNoPercent;
		}

		Core::Redraw::Request();
	}

	void LspClient::HandleBody(Shared& shared, const std::string& body)
	{
		std::optional<IncomingMessage> incoming;

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

			incoming = shared.Rpc.Receive(body);

			if (!incoming)
			{
				return;
			}

			if (incoming->RespondTo)
			{
				AnswerServerRequestLocked(shared, *incoming);
				return;
			}

			if (incoming->ReplyTo && *incoming->ReplyTo == shared.InitializeId)
			{
				CompleteHandshakeLocked(shared, *incoming);
				return;
			}
		}

		if (!incoming->Method.empty())
		{
			HandleNotification(shared, *incoming);
			return;
		}

		if (incoming->ReplyTo)
		{
			HandleReply(shared, *incoming);
		}
	}

	void LspClient::HandleNotification(Shared& shared, const IncomingMessage& notification)
	{
		if (notification.Method == "$/progress")
		{
			ApplyProgress(shared, notification.Payload);
			return;
		}

		if (notification.Method == "textDocument/publishDiagnostics")
		{
			PublishDiagnostics(shared, notification.Payload);
			return;
		}

		if (notification.Method == "window/showMessage")
		{
			RecordServerMessage(shared, notification.Payload);
			return;
		}

		if (shared.Adapter->HandleNotification(shared.Context, notification.Method, notification.Payload) ==
			NotificationEffect::ProjectsLoaded)
		{
			{
				const std::lock_guard<std::mutex> state(shared.StateMutex);

				shared.ProjectsLoading.reset();

				const bool answering = shared.Initialized && shared.Status.State != ServerState::Failed;

				if (answering && shared.Progress.empty())
				{
					Settle(shared);
					shared.Status.Percent = kNoPercent;
				}
			}

			// WARNING: StateMutex is released first because the two nest only the other way round.
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);
			PullShownLocked(shared);
		}
	}

	void LspClient::RecordServerMessage(Shared& shared, const nlohmann::json& params)
	{
		if (!params.is_object())
		{
			return;
		}

		const std::optional<ServerMessageKind> kind = ShownMessageKind(params);
		const nlohmann::json::const_iterator message = params.find("message");

		if (!kind || message == params.end() || !message->is_string())
		{
			return;
		}

		const std::lock_guard<std::mutex> state(shared.StateMutex);

		shared.Messages.push_back(ServerMessage{.Server = shared.Status.Name, .Kind = *kind, .Text = message->get<std::string>()});
	}

	void LspClient::PublishDiagnostics(Shared& shared, const nlohmann::json& params)
	{
		PublishedDiagnostics published = ParseDiagnostics(params);

		if (published.Path.empty())
		{
			return;
		}

		const std::string uri = ToFileUri(published.Path);

		// WARNING: ProtocolMutex is held across the open check and the push, so a CloseDocument lands either before the check or after the push, where it takes the publication back out.
		const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

		const Document* openDocument = FindDocument(shared, uri);

		if (openDocument == nullptr)
		{
			return;
		}

		published.Path = openDocument->Path;

		// WARNING: OmniSharp names version 0 and csharp-ls none, and this client never sends 0. Such a publication describes the version last sent, and read as written it would stay dimmed.
		if (published.Version == DocumentVersion{})
		{
			published.Version = DocumentVersion{openDocument->Sent};
		}

		const std::lock_guard<std::mutex> state(shared.StateMutex);
		shared.Diagnostics.push_back(std::move(published));
	}

	void LspClient::MarkSentLocked(Shared& shared, const std::string& uri, int version)
	{
		Document* document = FindDocument(shared, uri);

		if (document != nullptr)
		{
			document->Sent = version;
		}
	}

	void LspClient::PullLocked(Shared& shared, const std::string& uri)
	{
		Document* foundDocument = FindDocument(shared, uri);

		if (!shared.PullsDiagnostics || shared.ReaderStopped || foundDocument == nullptr ||
			foundDocument->Role != DocumentRole::Shown || foundDocument->Sent == kNothingSent)
		{
			return;
		}

		{
			// WARNING: Roslyn answers a pull sent before its projects load from a reading of the file outside any project, an error on every name the project defines.
			const std::lock_guard<std::mutex> state(shared.StateMutex);

			if (shared.ProjectsLoading)
			{
				return;
			}
		}

		Document& document = *foundDocument;

		if (document.Pull != kNoRequest)
		{
			document.PullAgain = true;
			return;
		}

		auto [id, bytes] = shared.Rpc.Request("textDocument/diagnostic", TextDocumentParams(uri));

		shared.Pending[id] = Asked{.Kind = Question::Diagnostics,
								   .Uri = uri,
								   .Version = DocumentVersion{document.Sent},
								   .Deadline = DeadlineFromNowLocked(shared, Question::Diagnostics)};
		document.Pull = id;
		document.PullAgain = false;

		SendLocked(shared, std::move(bytes));
	}

	void LspClient::PullShownLocked(Shared& shared)
	{
		for ([[maybe_unused]] const auto& [uri, document] : shared.Documents)
		{
			PullLocked(shared, uri);
		}
	}

	void LspClient::ReceivePull(Shared& shared, const Asked& asked, const IncomingMessage& reply)
	{
		const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

		Document* foundDocument = FindDocument(shared, asked.Uri);

		if (foundDocument == nullptr)
		{
			return;
		}

		Document& document = *foundDocument;
		document.Pull = kNoRequest;

		const bool retrigger = reply.IsError && !document.Retriggered && AsksToBeRetriggered(reply.Payload);

		if (!reply.IsError)
		{
			if (std::optional<std::vector<Diagnostic>> items = ParseDiagnosticReport(reply.Payload))
			{
				const std::lock_guard<std::mutex> state(shared.StateMutex);

				shared.Diagnostics.push_back(PublishedDiagnostics{.Path = document.Path, .Version = asked.Version, .Items = std::move(*items)});
				shared.Recolour.push_back(document.Path);
			}
		}

		if (document.PullAgain || retrigger)
		{
			PullLocked(shared, asked.Uri);
		}

		// WARNING: Set after the pull it describes has gone out, because PullLocked knows nothing of why it was called.
		document.Retriggered = retrigger;
	}

	void LspClient::NameOpenDocumentsAsOpenedLocked(const Shared& shared, std::vector<Location>& locations)
	{
		for (Location& location : locations)
		{
			const Document* openDocument = FindDocument(shared, ToFileUri(location.Path));

			if (openDocument != nullptr)
			{
				location.Path = openDocument->Path;
			}
		}
	}

	void LspClient::HandleReply(Shared& shared, const IncomingMessage& reply)
	{
		Asked asked;

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

			PendingTable::node_type pending = shared.Pending.extract(*reply.ReplyTo);

			if (pending.empty())
			{
				return;
			}

			asked = std::move(pending.mapped());
		}

		if (asked.Kind == Question::Diagnostics)
		{
			ReceivePull(shared, asked, reply);
			return;
		}

		// WARNING: Columns stay in the protocol's UTF-16 units. The caller converts them against the buffer it moves the caret in, the only copy certainly on screen.
		Answer answer = ReadAnswer(asked, reply, shared.SemanticLegend);

		if (asked.Kind == Question::Hover)
		{
			answer.Hover = shared.Adapter->ParseHover(answer.Text);
		}

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);
			NameOpenDocumentsAsOpenedLocked(shared, answer.Locations);
		}

		if (asked.Kind == Question::References && asked.From)
		{
			const FilePosition& from = *asked.From;

			std::erase_if(answer.Locations, [&from](const Location& location) { return IsPositionAskedFrom(location, from); });
		}

		const std::lock_guard<std::mutex> state(shared.StateMutex);
		shared.Answers.push_back(std::move(answer));
	}

	LspClient::Answer LspClient::ReadAnswer(const Asked& asked, const IncomingMessage& reply,
											const std::vector<SemanticKind>& legend)
	{
		if (reply.IsError)
		{
			return FailedAnswer(*reply.ReplyTo, ReplyFailure::ServerError);
		}

		Answer answer;
		answer.Id = *reply.ReplyTo;

		if (asked.Kind == Question::Symbols)
		{
			answer.Declarations = ParseSymbols(reply.Payload);
			return answer;
		}

		if (asked.Kind == Question::SemanticTokens)
		{
			answer.SemanticTokens = ParseSemanticTokens(reply.Payload, legend);
			return answer;
		}

		if (asked.Kind == Question::SwitchSourceHeader)
		{
			if (std::optional<Location> location = ParseSwitchSourceHeader(reply.Payload))
			{
				answer.Locations.push_back(std::move(*location));
			}

			return answer;
		}

		if (asked.Kind == Question::Hover)
		{
			if (reply.Payload.is_object())
			{
				const nlohmann::json::const_iterator contents = reply.Payload.find("contents");

				if (contents != reply.Payload.end() && contents->is_object())
				{
					const nlohmann::json::const_iterator value = contents->find("value");

					if (value != contents->end() && value->is_string())
					{
						answer.Text = value->get<std::string>();
					}
				}
			}

			return answer;
		}

		answer.Locations = ParseLocations(reply.Payload);

		return answer;
	}

	void LspClient::AnswerServerRequestLocked(Shared& shared, const IncomingMessage& request)
	{
		const nlohmann::json& id = *request.RespondTo;

		if (request.Method == "workspace/configuration")
		{
			SendNowLocked(shared, shared.Rpc.Reply(id, ConfigurationReply(*shared.Adapter, shared.Context, request.Payload)));
			return;
		}

		if (request.Method == "workspace/diagnostic/refresh")
		{
			SendNowLocked(shared, shared.Rpc.Reply(id, nullptr));
			PullShownLocked(shared);
			return;
		}

		if (request.Method == "window/showMessageRequest")
		{
			RecordServerMessage(shared, request.Payload);
		}

		if (IsAcknowledgedWithNull(request.Method))
		{
			SendNowLocked(shared, shared.Rpc.Reply(id, nullptr));
			return;
		}

		if (std::optional<nlohmann::json> answer = shared.Adapter->AnswerRequest(shared.Context, request.Method, request.Payload))
		{
			SendNowLocked(shared, shared.Rpc.Reply(id, *answer));
			return;
		}

		// WARNING: Refused rather than ignored, because a server that asked and heard nothing waits forever.
		SendNowLocked(shared, shared.Rpc.ErrorReply(id, kMethodNotFound, "method not found"));
	}

	void LspClient::CompleteHandshakeLocked(Shared& shared, const IncomingMessage& reply)
	{
		if (reply.IsError)
		{
			shared.HandshakeFailed = true;
			shared.Queued.clear();

			const std::lock_guard<std::mutex> state(shared.StateMutex);

			shared.Status.State = ServerState::Failed;
			shared.Status.Detail = DescribeError(reply.Payload);
			shared.Status.Percent = kNoPercent;

			return;
		}

		shared.Initialized = true;

		const nlohmann::json& result = reply.Payload;

		shared.SemanticLegend = ParseSemanticLegend(result);
		shared.PullsDiagnostics = OffersDiagnosticPulls(result);

		std::string detail;

		if (result.is_object())
		{
			const nlohmann::json::const_iterator info = result.find("serverInfo");

			if (info != result.end() && info->is_object())
			{
				const nlohmann::json::const_iterator name = info->find("name");
				const nlohmann::json::const_iterator version = info->find("version");

				if (name != info->end() && name->is_string())
				{
					detail = name->get<std::string>();
				}

				if (version != info->end() && version->is_string())
				{
					detail += detail.empty() ? version->get<std::string>()
											 : " " + version->get<std::string>();
				}
			}
		}

		SendNowLocked(shared, shared.Rpc.Notification("initialized", nlohmann::json::object()));

		std::vector<OutgoingNotification> afterInitialized;
		shared.Adapter->AfterInitialized(shared.Context, afterInitialized);

		for (const OutgoingNotification& notification : afterInitialized)
		{
			SendNowLocked(shared, shared.Rpc.Notification(notification.Method, notification.Params));
		}

		for (std::string& queued : shared.Queued)
		{
			SendNowLocked(shared, std::move(queued));
		}

		shared.Queued.clear();

		StartWaitingDeadlinesLocked(shared);

		PullShownLocked(shared);

		const std::lock_guard<std::mutex> state(shared.StateMutex);

		Settle(shared);

		if (!detail.empty())
		{
			shared.Status.Detail = detail;
		}
	}

	void LspClient::ApplyProgress(Shared& shared, const nlohmann::json& params)
	{
		if (!params.is_object())
		{
			return;
		}

		const nlohmann::json::const_iterator value = params.find("value");

		if (value == params.end() || !value->is_object())
		{
			return;
		}

		const nlohmann::json::const_iterator kind = value->find("kind");

		if (kind == value->end() || !kind->is_string())
		{
			return;
		}

		const std::string stage = kind->get<std::string>();

		int percent = kNoPercent;
		const nlohmann::json::const_iterator percentage = value->find("percentage");

		if (percentage != value->end() && percentage->is_number())
		{
			percent = static_cast<int>(percentage->get<double>());
		}

		const nlohmann::json::const_iterator token = params.find("token");
		const nlohmann::json tokenId = token != params.end() ? *token : nlohmann::json();

		const std::string title = StringField(*value, "title");
		const std::string message = StringField(*value, "message");

		const std::lock_guard<std::mutex> state(shared.StateMutex);

		std::vector<Shared::RunningProgress>& running = shared.Progress;

		Shared::RunningProgress* const found = FindProgress(running, tokenId);

		if (stage == "begin")
		{
			if (found == nullptr)
			{
				running.push_back(Shared::RunningProgress{.Token = tokenId, .Title = title, .Message = message});
			}

			shared.Status.State = ServerState::Indexing;
			shared.Status.Percent = percent < 0 ? 0 : percent;
			shared.Status.Activity = ActivityOf(running);
			return;
		}

		if (stage == "report")
		{
			if (percent >= 0)
			{
				shared.Status.Percent = percent;
			}

			if (found != nullptr && !message.empty())
			{
				found->Message = message;
			}

			shared.Status.Activity = ActivityOf(running);
			return;
		}

		if (stage == "end")
		{
			std::erase_if(running, [&tokenId](const Shared::RunningProgress& progress) { return IsProgressOf(progress, tokenId); });
			shared.Status.Activity = ActivityOf(running);

			if (!running.empty())
			{
				return;
			}

			Settle(shared);
			shared.Status.Percent = kNoPercent;
		}
	}

	bool LspClient::IsProgressOf(const Shared::RunningProgress& progress, const nlohmann::json& token)
	{
		return progress.Token == token;
	}

	LspClient::Shared::RunningProgress* LspClient::FindProgress(std::vector<Shared::RunningProgress>& running, const nlohmann::json& token)
	{
		for (Shared::RunningProgress& progress : running)
		{
			if (IsProgressOf(progress, token))
			{
				return &progress;
			}
		}

		return nullptr;
	}

	std::string LspClient::ActivityOf(const std::vector<Shared::RunningProgress>& running)
	{
		if (running.empty())
		{
			return {};
		}

		const Shared::RunningProgress& latest = running.back();

		if (latest.Message.empty())
		{
			return latest.Title;
		}

		return latest.Title.empty() ? latest.Message : latest.Title + ": " + latest.Message;
	}
}

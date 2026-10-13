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

		// Null to workDoneProgress/create means "report under that token", and null to showMessageRequest means no action was chosen; refusing a registration stops some servers.
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

		// A failed pull may carry data.retriggerRequest: the server cancelled it (Roslyn does when
		// its solution moved under the request) and expects to be asked again.
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

		nlohmann::json InitializeParams(const std::filesystem::path& root, const nlohmann::json& initializationOptions)
		{
			nlohmann::json capabilities;

			// Declaring workDoneProgress is what makes clangd report its indexing at all: without
			// it there is no window/workDoneProgress/create and no $/progress, and the status bar
			// has no percentage to show.
			capabilities["window"]["workDoneProgress"] = true;
			capabilities["workspace"]["configuration"] = true;

			capabilities["textDocument"]["synchronization"]["didSave"] = true;
			capabilities["textDocument"]["synchronization"]["dynamicRegistration"] = false;

			// linkSupport left off deliberately: with it on, a definition comes back as a
			// LocationLink whose several ranges mean different things, and only one of them is the
			// place a reader wants to land.
			capabilities["textDocument"]["definition"]["linkSupport"] = false;

			// Declared, because the protocol has the client name the token types it understands and
			// the server answer with a legend drawn from them. Only the ones this editor colours
			// are listed - a type it never asked for is one it would have to map to nothing anyway.
			capabilities["textDocument"]["semanticTokens"]["requests"]["full"] = true;
			capabilities["textDocument"]["semanticTokens"]["formats"] = nlohmann::json::array({"relative"});
			capabilities["textDocument"]["semanticTokens"]["tokenTypes"] = nlohmann::json::array(
				{"namespace", "type", "class", "enum", "interface", "struct", "typeParameter",
				 "concept", "parameter", "variable", "property", "enumMember", "function",
				 "method", "macro", "event", "field", "fieldName", "constant", "constantName",
				 "extensionMethod", "extensionMethodName", "recordClass", "recordStruct",
				 "delegate", "delegateName"});
			capabilities["textDocument"]["semanticTokens"]["tokenModifiers"] = nlohmann::json::array();

			// Pulled, not pushed, from a server that offers it: Roslyn answers only pulls, and
			// csharp-ls stops publishing once this is declared. Dynamic registration is refused so
			// the offer arrives in the handshake, where CompleteHandshake reads it.
			capabilities["textDocument"]["diagnostic"]["dynamicRegistration"] = false;
			capabilities["textDocument"]["diagnostic"]["relatedDocumentSupport"] = false;

			// Roslyn asks for fresh pulls through this once its projects have loaded and whenever
			// its solution changes under it.
			capabilities["workspace"]["diagnostics"]["refreshSupport"] = true;
			// Markdown first: clangd's markdown reply is the one ParseClangdHover reads into
			// fields.
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

		// The whole of what an error reply says, for a status line. A server that refuses to start
		// says why in "message", and dropping that leaves a user with a red status and no reason.
		std::string DescribeError(const nlohmann::json& error)
		{
			if (error.is_object())
			{
				const auto message = error.find("message");

				if (message != error.end() && message->is_string())
				{
					return message->get<std::string>();
				}
			}

			return "the language server refused to start";
		}
	}

	void LspClient::SendNowLocked(Shared& shared, std::string bytes)
	{
		// Queued, not written: writing here would be a synchronous write under ProtocolMutex, and
		// a pipe that is full because the server is busy writing to us would stop this thread with
		// the mutex held — including when "this thread" is the reader, which is the one thread
		// that must never stop. The writer thread takes it from here.
		{
			const std::lock_guard<std::mutex> outbound(shared.OutboundMutex);

			if (shared.WriterStopped)
			{
				// Dropped rather than kept: a didChange for a server that will never read it has
				// nothing worth preserving, and keeping it costs a copy of the file per keystroke.
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
				// The handshake will never finish — refused, timed out, or never sent. Dropped
				// rather than queued, and whatever is already queued is dropped with it: the queue
				// stopped being a queue the moment nothing was ever going to drain it, and a
				// session that keeps typing at such a server would append a whole copy of the file
				// to it on every keystroke.
				shared.Queued.clear();
				return;
			}

			{
				const std::lock_guard<std::mutex> outbound(shared.OutboundMutex);

				if (shared.WriterStopped)
				{
					// The other half of the same leak: a server that died during its own handshake
					// leaves Initialized false forever, so without this every later notification
					// would pile up here instead of in ToSend.
					shared.Queued.clear();
					return;
				}
			}

			// Nothing but the handshake may go out before initialize is answered: a server is
			// entitled to drop, or fail on, anything that arrives first. The queue keeps the
			// order the caller asked in.
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

		// A writer parked on the condition variable has no other way to notice.
		shared.Outbound.notify_all();
	}

	void LspClient::BeginHandshake(const std::filesystem::path& root)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		// Recorded before it is queued, and under the same mutex the reader takes to read it, so
		// the reply can never arrive at a reader that does not yet know which id the handshake
		// used.
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

			// No process means no reader and no writer, so nothing will ever fail a question the
			// way the reader's exit does, and nothing will ever empty the outbound queue. Ask
			// checks the first flag; every sender checks the second, or a session spent typing at
			// a server that never started would queue every edit forever.
			{
				const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);
				m_Shared->ReaderStopped = true;

				// No process means no handshake was ever sent, so there is no deadline to run out
				// and nothing will ever set Initialized. Said here so SendLocked drops rather than
				// queues from the very first call, and so the timeout below has nothing to expire
				// and cannot overwrite the real reason this failed.
				m_Shared->HandshakeFailed = true;
			}

			StopOutbound(*m_Shared);

			return;
		}

		m_Shared->Process = Core::Ref<Core::ChildProcess>(std::move(started.GetValue()));

		// Queued before either thread exists, so nothing that can throw comes after them: the
		// writer sends it once it starts, and no reply can reach a reader that is not yet running.
		BeginHandshake(root);

		// Starting a thread can throw std::system_error. The destructor never runs for a
		// constructor that throws, and a joinable std::thread destroyed unjoined ends the process,
		// so what was started is stopped here before the exception goes on.
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

		// Everything the real constructor does except start a process and the two threads that
		// serve it. The handshake still goes out, into a queue nobody drains, so a test can read
		// what would have been sent and answer it through Deliver.
		BeginHandshake(root);
	}

	LspClient::~LspClient()
	{
		StopThreads();
	}

	void LspClient::StopThreads()
	{
		m_Shared->Stopping.store(true);

		// Killing the child is what releases both threads at once: it closes the pipe the reader
		// is blocked reading, and it breaks the pipe the writer may be blocked writing into.
		// Waiting here for the server to leave on its own would hold the UI thread for as long as
		// a wedged server felt like; ShutDown is where it gets that time, off the UI thread.
		if (m_Shared->Process)
		{
			m_Shared->Process->Terminate();
		}

		// Woken as well as terminated: a writer parked on the condition variable has nothing to
		// notice a dead pipe with.
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

			// Continues from the last version this client ever sent for this URI rather than
			// restarting at 1: a reopen after a close is still this same client's numbering, and a
			// publication that was already in flight for the pre-close version must not be read as
			// answering a version this reopen has not reached yet (see LastVersions' own comment).
			int& lastVersion = m_Shared->LastVersions[uri];
			++lastVersion;

			Document& document = m_Shared->Documents[uri];
			document.Version = lastVersion;
			document.Path = path;
			document.Role = role;
			version = lastVersion;
		}

		// Built outside ProtocolMutex: it copies and escapes the whole file, and the reader takes
		// that mutex for every message the server sends. Queued in version order all the same,
		// because a document's one owner (ADR 0512) calls this and ChangeDocument on one thread.
		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["languageId"] = m_Shared->ProtocolLanguageId;
		params["textDocument"]["version"] = version;
		params["textDocument"]["text"] = text;

		std::string bytes = JsonRpc::Notification("textDocument/didOpen", params);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);
		SendLocked(*m_Shared, std::move(bytes));
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

			const auto found = m_Shared->Documents.find(uri);

			if (found == m_Shared->Documents.end())
			{
				// A change to a document the server was never told about would be a change to nothing.
				return DocumentVersion{};
			}

			version = ++found->second.Version;
		}

		// Full text, not a range. An incremental change describes an edit the server applies to
		// its own copy, and a range computed even slightly wrong corrupts that copy silently —
		// every answer afterwards is subtly about a file nobody has. The text of one source file
		// costs nothing next to that.
		nlohmann::json change;
		change["text"] = text;

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["version"] = version;
		params["contentChanges"] = nlohmann::json::array({std::move(change)});

		// Built outside ProtocolMutex for the reason OpenDocument gives.
		std::string bytes = JsonRpc::Notification("textDocument/didChange", params);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);
		SendLocked(*m_Shared, std::move(bytes));

		// Every shown document, not only this one: a C# file's diagnostics depend on the rest of
		// its project whatever a server declares (csharp-ls says interFileDependencies is false).
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

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didSave", params));
	}

	void LspClient::CloseDocument(const std::filesystem::path& path)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		const auto open = m_Shared->Documents.find(uri);

		if (open == m_Shared->Documents.end())
		{
			return;
		}

		// A pull in flight asks about a document that is going: cancelled, and forgotten, so an
		// answer still on its way is dropped like any cancelled reply.
		if (const RequestId pull = open->second.Pull; pull != 0)
		{
			m_Shared->Pending.erase(pull);
			SendLocked(*m_Shared, m_Shared->Rpc.Cancel(pull));
		}

		m_Shared->Documents.erase(open);

		// Publications for this document that arrived while it was open but have not been drained
		// yet go with it. Drained after the close, they would reach DiagnosticsModel in the same
		// update as the close's own note and be applied after it, recreating the entry the close
		// erases. Together with PublishDiagnostics holding ProtocolMutex across its open check and
		// its push, nothing published for this path survives the close.
		{
			const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

			std::erase_if(m_Shared->Diagnostics, [&uri](const PublishedDiagnostics& published) { return ToFileUri(published.Path) == uri; });
		}

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didClose", params));
	}

	RequestId LspClient::Ask(std::string_view method, const nlohmann::json& params,
							 Question question, std::optional<FilePosition> from)
	{
		auto [id, bytes] = m_Shared->Rpc.Request(method, params);

		if (m_Shared->ReaderStopped)
		{
			// There is no reader left to bring an answer back — either the server never started or
			// it has since died. Answering here rather than never is the whole point: a caller
			// holding a ticket has no other way to find out, and a ticket nothing ever fails is a
			// spinner that turns forever.
			m_Shared->Rpc.Forget(id);

			const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

			Answer answer;
			answer.Id = id;
			answer.Failed = true;

			m_Shared->Answers.push_back(std::move(answer));

			return id;
		}

		m_Shared->Pending[id] = Asked{question, std::move(from)};
		SendLocked(*m_Shared, std::move(bytes));

		return id;
	}

	RequestId LspClient::AskDefinition(const FilePosition& where)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = ToFileUri(where.Path);
		params["position"]["line"] = where.Line;
		params["position"]["character"] = where.Column.Value;

		return Ask("textDocument/definition", params, Question::Definition);
	}

	RequestId LspClient::AskHover(const FilePosition& where)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = ToFileUri(where.Path);
		params["position"]["line"] = where.Line;
		params["position"]["character"] = where.Column.Value;

		return Ask("textDocument/hover", params, Question::Hover);
	}

	RequestId LspClient::AskSwitchSourceHeader(const std::filesystem::path& path)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["uri"] = ToFileUri(path);

		return Ask("textDocument/switchSourceHeader", params, Question::SwitchSourceHeader);
	}

	RequestId LspClient::AskReferences(const FilePosition& where)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = ToFileUri(where.Path);
		params["position"]["line"] = where.Line;
		params["position"]["character"] = where.Column.Value;
		// Every site, the declaration included — a symbol used nowhere still has a header worth
		// jumping to, and an empty answer is a worse one than a short answer. The position asked
		// from is dropped when the reply is read, so what comes back is every OTHER place: the
		// count over a definition and the list opened from it then agree, and neither shows the
		// line the question was asked on.
		params["context"]["includeDeclaration"] = true;

		return Ask("textDocument/references", params, Question::References, where);
	}

	RequestId LspClient::AskSemanticTokens(const std::filesystem::path& path)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		// No legend, no question. A server that declared no semantic tokens would answer with
		// nothing or with numbers this side could not read, and either way the request is one the
		// editor would be waiting on for no reason.
		if (m_Shared->SemanticLegend.empty())
		{
			return 0;
		}

		nlohmann::json params;
		params["textDocument"]["uri"] = ToFileUri(path);

		return Ask("textDocument/semanticTokens/full", params, Question::SemanticTokens);
	}

	RequestId LspClient::AskSymbols(const std::filesystem::path& path)
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = ToFileUri(path);

		return Ask("textDocument/documentSymbol", params, Question::Symbols);
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

		// Numbered and queued like any request, then immediately forgotten: nothing here will
		// ever read the reply, and leaving the id outstanding would only make JsonRpc's table
		// grow for an object about to be destroyed anyway.
		auto [id, bytes] = m_Shared->Rpc.Request("shutdown", nullptr);
		m_Shared->Rpc.Forget(id);
		SendLocked(*m_Shared, std::move(bytes));

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("exit", nullptr));

		return m_Shared->Initialized;
	}

	void LspClient::ShutDown(std::chrono::milliseconds grace)
	{
		const bool serverWasAsked = RequestShutdown();

		// The writer sends everything already queued before it stops, exit included.
		m_Shared->Stopping.store(true);
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

			// Everything held back for a reply that is not coming. Cleared here as well as in
			// SendLocked, so the memory goes back now rather than on the next thing sent — a
			// server that dies during its handshake may never be sent anything again.
			m_Shared->Queued.clear();
		}

		// ProtocolMutex released first: the two are only ever taken in that order, and nothing
		// here needs both at once.
		const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

		m_Shared->Status.State = ServerState::Failed;
		m_Shared->Status.Detail = "no answer to initialize within " +
								  std::to_string(kHandshakeTimeout.count()) + " seconds";
		m_Shared->Status.Percent = -1;
	}

	std::size_t LspClient::QueuedCount() const
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		return m_Shared->Queued.size();
	}

	void LspClient::Drain(Drained& drained)
	{
		// Before the status is read out, so a handshake that ran out of time between this frame
		// and the last is reported as Failed on this one rather than the next.
		ExpireHandshakeIfStalled(std::chrono::steady_clock::now());

		// The only place anything crosses to the UI thread, and it touches nothing the server can
		// hold up: StateMutex is never held across a read from, or a write to, the pipe.
		const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

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
			// Refused rather than trusted to a comment: on a client with a writer thread this
			// would steal messages out from under it, losing protocol traffic with no symptom at
			// all. Only the constructor that starts no process leaves anything readable here.
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

				shared->Outbound.wait(outbound,
									  [&shared] {
										  return !shared->ToSend.empty() ||
												 shared->Stopping.load() || shared->WriterStopped;
									  });

				if (shared->WriterStopped)
				{
					// The reader got there first: it saw the pipe close and closed the queue.
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

			// Outside every lock. This is the call that blocks when the server is not reading, and
			// blocking here costs nothing: the reader keeps draining the pipe, which is what lets
			// the server get back to reading ours.
			for (const std::string& message : batch)
			{
				if (!process.Write(message))
				{
					// The pipe is gone. The reader will see the same thing and report it; there is
					// nothing this thread can usefully do but stop trying.
					StopOutbound(*shared);

					return;
				}
			}
		}
	}

	void LspClient::RunReader(Core::Ref<Shared> shared)
	{
		MessageStream stream;
		std::array<char, 8192> buffer{};

		// Read without any mutex held: it blocks for as long as the server stays quiet, and holding
		// a mutex across that would stop every UI-thread call dead. The pointer itself is set
		// before this thread starts and never reassigned.
		Core::ChildProcess& process = *shared->Process;

		for (;;)
		{
			// Zero means the child closed its output, which is how this loop learns the server is
			// gone without ever polling IsRunning.
			const std::size_t read = process.Read(buffer);

			if (read == 0)
			{
				break;
			}

			stream.Append(std::string_view(buffer.data(), read));

			while (std::optional<std::string> body = stream.Take())
			{
				HandleBody(*shared, *body);
			}

			// Once per read rather than per message: whatever the batch changed — answers,
			// diagnostics, progress — is drained by the same frame.
			Core::Redraw::Request();
		}

		std::vector<RequestId> abandoned;

		{
			const std::lock_guard<std::mutex> protocol(shared->ProtocolMutex);

			for (const auto& entry : shared->Pending)
			{
				abandoned.push_back(entry.first);
			}

			shared->Pending.clear();

			// Set here, in the same breath as the abandonment and under the same mutex Ask holds:
			// a question asked at this instant either got into Pending before this ran and is
			// failed below, or arrives after and is failed by Ask. Never neither.
			shared->ReaderStopped = true;
		}

		// The writer has nothing left to carry once the pipe is gone, and — the part that matters
		// for a session that keeps running — every sender after this point has to drop rather than
		// queue. A dead server would otherwise collect a whole file's text per keystroke, forever,
		// because notifications have no ticket to fail the way requests do.
		StopOutbound(*shared);

		const std::lock_guard<std::mutex> state(shared->StateMutex);

		// Everything still outstanding is answered as failed rather than left hanging: a caller
		// holding a ticket has no other way to learn the server it was asked of is gone.
		for (const RequestId id : abandoned)
		{
			Answer answer;
			answer.Id = id;
			answer.Failed = true;

			shared->Answers.push_back(std::move(answer));
		}

		if (!shared->Stopping.load())
		{
			shared->Status.State = ServerState::Failed;
			shared->Status.Detail = "the language server stopped";
			shared->Status.Percent = -1;
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
				AnswerServerRequest(shared, *incoming);
				return;
			}

			if (incoming->ReplyTo && *incoming->ReplyTo == shared.InitializeId)
			{
				CompleteHandshake(shared, *incoming);
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

				// A server whose handshake failed answers nothing, whatever it says about its projects.
				const bool answering = shared.Initialized && shared.Status.State != ServerState::Failed;

				if (answering && shared.ProgressTokens.empty())
				{
					Settle(shared);
					shared.Status.Percent = -1;
				}
			}

			// After StateMutex is released, since the two nest only the other way round: the pulls
			// PullLocked held back while the projects loaded.
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);
			PullShownLocked(shared);
		}

		// Anything else - window/logMessage, telemetry, a server's own notifications - may be dropped.
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

		// Dropped unless this client holds the document open. A publication can outlive its
		// didClose - the notification is already in flight when the close goes out - and the
		// warm-up in LanguageServices::Poll opens and closes a file deliberately.
		// Shared::Documents is the authority on what is open and is already erased on close, so
		// this check costs a lookup.
		const std::string uri = ToFileUri(published.Path);

		// ProtocolMutex is held across both the check and the push, so a CloseDocument for this
		// path lands either before the check - and the publication is dropped here - or after the
		// push, and CloseDocument takes what was pushed back out. Without that, a close landing
		// between the two let a publication for a document that had just closed through, and
		// DiagnosticsModel's close-then-publish order recreated an entry no later Closed would ever
		// erase. Nesting StateMutex inside ProtocolMutex is the order Ask, CompleteHandshake and
		// CloseDocument already take them in, and nothing takes them the other way round.
		const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

		const std::unordered_map<std::string, Document>::const_iterator open = shared.Documents.find(uri);

		if (open == shared.Documents.end())
		{
			return;
		}

		published.Path = open->second.Path;

		// OmniSharp names version 0 and csharp-ls none at all, and this client never sends 0, so
		// such a publication can only describe the text the server has: the version last sent
		// (ADR 0584). Read as written it would compare older than everything sent and stay dimmed.
		if (published.Version == DocumentVersion{})
		{
			published.Version = DocumentVersion{open->second.Version};
		}

		const std::lock_guard<std::mutex> state(shared.StateMutex);
		shared.Diagnostics.push_back(std::move(published));
	}

	void LspClient::PullLocked(Shared& shared, const std::string& uri)
	{
		const auto found = shared.Documents.find(uri);

		if (!shared.PullsDiagnostics || shared.ReaderStopped || found == shared.Documents.end() ||
			found->second.Role != DocumentRole::Shown)
		{
			return;
		}

		{
			// Roslyn would answer from a reading of the file outside any project, an error on every
			// name the project defines. ProjectsLoaded pulls everything held back here.
			const std::lock_guard<std::mutex> state(shared.StateMutex);

			if (shared.ProjectsLoading)
			{
				return;
			}
		}

		Document& document = found->second;

		if (document.Pull != 0)
		{
			document.PullAgain = true;
			return;
		}

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;

		auto [id, bytes] = shared.Rpc.Request("textDocument/diagnostic", params);

		shared.Pending[id] = Asked{.Kind = Question::Diagnostics, .Uri = uri, .Version = DocumentVersion{document.Version}};
		document.Pull = id;
		document.PullAgain = false;

		SendLocked(shared, std::move(bytes));
	}

	void LspClient::PullShownLocked(Shared& shared)
	{
		for (const auto& entry : shared.Documents)
		{
			PullLocked(shared, entry.first);
		}
	}

	void LspClient::ReceivePull(Shared& shared, const Asked& asked, const IncomingMessage& reply)
	{
		const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

		const auto found = shared.Documents.find(asked.Uri);

		if (found == shared.Documents.end())
		{
			return;
		}

		Document& document = found->second;
		document.Pull = 0;

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

		// Set after the pull it describes has gone out: PullLocked knows nothing of why it was called.
		document.Retriggered = retrigger;
	}

	void LspClient::NameOpenDocumentsAsOpened(const Shared& shared, std::vector<Location>& locations)
	{
		for (Location& location : locations)
		{
			const std::unordered_map<std::string, Document>::const_iterator open = shared.Documents.find(ToFileUri(location.Path));

			if (open != shared.Documents.end())
			{
				location.Path = open->second.Path;
			}
		}
	}

	void LspClient::HandleReply(Shared& shared, const IncomingMessage& reply)
	{
		Asked asked;

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

			const auto found = shared.Pending.find(*reply.ReplyTo);

			if (found == shared.Pending.end())
			{
				// Cancelled while it was in flight; the caller has moved on.
				return;
			}

			asked = found->second;
			shared.Pending.erase(found);
		}

		if (asked.Kind == Question::Diagnostics)
		{
			ReceivePull(shared, asked, reply);
			return;
		}

		// Parse, correlate, hand over. The columns stay in the protocol's UTF-16 units, and the
		// caller converts them against the buffer it is about to move the caret in — which is the
		// only copy of the file that is certainly the one the user is looking at.
		Answer answer = ReadAnswer(asked, reply, shared.SemanticLegend);

		if (asked.Kind == Question::Hover)
		{
			answer.Hover = shared.Adapter->ParseHover(answer.Text);
		}

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);
			NameOpenDocumentsAsOpened(shared, answer.Locations);
		}

		// Nobody is shown the line they asked from. The protocol counts a declaration as a
		// reference to itself, so a count drawn over a definition would otherwise include that
		// definition and read one too high, and the list opened from it would lead its results
		// with the row already on screen. Only for references: a definition reply naming the
		// place asked about is the answer, not noise, and ResolveDefinition is what decides there
		// is nowhere to go.
		if (asked.Kind == Question::References && asked.From)
		{
			const FilePosition& from = *asked.From;

			std::erase_if(answer.Locations, [&from](const Location& location) {
				return location.Line == from.Line && location.Column == from.Column &&
					   location.Path == from.Path;
			});
		}

		const std::lock_guard<std::mutex> state(shared.StateMutex);
		shared.Answers.push_back(std::move(answer));
	}

	LspClient::Answer LspClient::ReadAnswer(const Asked& asked, const IncomingMessage& reply,
											const std::vector<SemanticKind>& legend)
	{
		Answer answer;
		answer.Id = *reply.ReplyTo;
		answer.Failed = reply.IsError;

		if (answer.Failed)
		{
			return answer;
		}

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
			if (auto location = ParseSwitchSourceHeader(reply.Payload))
			{
				answer.Locations.push_back(std::move(*location));
			}

			return answer;
		}

		if (asked.Kind == Question::Hover)
		{
			// Null, a missing contents, or a non-markdown shape all read as "nothing to say", not
			// as a failure: clangd answers null on whitespace and on most punctuation.
			if (reply.Payload.is_object())
			{
				const auto contents = reply.Payload.find("contents");

				if (contents != reply.Payload.end() && contents->is_object())
				{
					if (const auto value = contents->find("value");
						value != contents->end() && value->is_string())
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

	void LspClient::AnswerServerRequest(Shared& shared, const IncomingMessage& request)
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

		// Refused rather than ignored: a server that asked and heard nothing waits forever.
		SendNowLocked(shared, shared.Rpc.ErrorReply(id, kMethodNotFound, "method not found"));
	}

	void LspClient::CompleteHandshake(Shared& shared, const IncomingMessage& reply)
	{
		if (reply.IsError)
		{
			// A server that refused to start is not Ready, and nothing queued behind the handshake
			// will ever be read by anyone. Keying the dispatch on the id alone and assuming success
			// would report a working server for a process that has already given up.
			// Said before the status, and under the mutex this already holds: Initialized never
			// becomes true after a refusal, so every notification sent from here on would queue
			// behind a handshake that has already ended.
			shared.HandshakeFailed = true;
			shared.Queued.clear();

			const std::lock_guard<std::mutex> state(shared.StateMutex);

			shared.Status.State = ServerState::Failed;
			shared.Status.Detail = DescribeError(reply.Payload);
			shared.Status.Percent = -1;

			return;
		}

		shared.Initialized = true;

		const nlohmann::json& result = reply.Payload;

		// Read once, here, because this is the only message that carries it. A reply's raw
		// integers mean nothing without it, and it is server-specific: the same index is a macro
		// to one server and a parameter to another.
		shared.SemanticLegend = ParseSemanticLegend(result);
		shared.PullsDiagnostics = OffersDiagnosticPulls(result);

		std::string detail;

		if (result.is_object())
		{
			const auto info = result.find("serverInfo");

			if (info != result.end() && info->is_object())
			{
				const auto name = info->find("name");
				const auto version = info->find("version");

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

		// Everything the caller asked for while the handshake was in flight, in the order it was
		// asked, now that the server will accept it.
		for (std::string& queued : shared.Queued)
		{
			SendNowLocked(shared, std::move(queued));
		}

		shared.Queued.clear();

		// Every document a tab opened while the handshake was in flight, now that the server has
		// said whether it answers pulls.
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

		const auto value = params.find("value");

		if (value == params.end() || !value->is_object())
		{
			return;
		}

		const auto kind = value->find("kind");

		if (kind == value->end() || !kind->is_string())
		{
			return;
		}

		const std::string stage = kind->get<std::string>();

		int percent = -1;
		const auto percentage = value->find("percentage");

		if (percentage != value->end() && percentage->is_number())
		{
			percent = static_cast<int>(percentage->get<double>());
		}

		// Null when the server named none, which then counts as one token like any other.
		const auto token = params.find("token");
		const nlohmann::json tokenId = token != params.end() ? *token : nlohmann::json();

		const std::lock_guard<std::mutex> state(shared.StateMutex);

		std::vector<nlohmann::json>& running = shared.ProgressTokens;

		if (stage == "begin")
		{
			if (std::find(running.begin(), running.end(), tokenId) == running.end())
			{
				running.push_back(tokenId);
			}

			shared.Status.State = ServerState::Indexing;
			shared.Status.Percent = percent < 0 ? 0 : percent;
			return;
		}

		if (stage == "report")
		{
			if (percent >= 0)
			{
				shared.Status.Percent = percent;
			}

			return;
		}

		if (stage == "end")
		{
			std::erase(running, tokenId);

			// Something else is still running. An end for a token never seen to begin still
			// settles the status when nothing else is: staying Indexing with nothing left that
			// could ever end it is the worse of the two mistakes.
			if (!running.empty())
			{
				return;
			}

			Settle(shared);
			shared.Status.Percent = -1;
		}
	}
}

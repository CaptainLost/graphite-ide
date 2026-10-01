#include "Lang/Server/LspClient.h"

#include "Core/App/Redraw.h"
#include "Core/Fs/FileSystem.h"
#include "Lang/Server/MessageStream.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		// The state a server settles into once it has answered initialize: a root with no compile
		// database is not a failure, but it is not Ready either — clangd runs there and answers
		// every question with a guess, which the status bar has to be able to say out loud.
		ServerState SettledState(bool noCompileDatabase)
		{
			return noCompileDatabase ? ServerState::NoCompileDatabase : ServerState::Ready;
		}

		nlohmann::json InitializeParams(const std::filesystem::path& root)
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
				 "method", "macro"});
			capabilities["textDocument"]["semanticTokens"]["tokenModifiers"] = nlohmann::json::array();
			capabilities["textDocument"]["references"]["dynamicRegistration"] = false;
			capabilities["textDocument"]["documentSymbol"]["hierarchicalDocumentSymbolSupport"] =
				true;

			nlohmann::json params;
			params["processId"] = nullptr;
			params["rootUri"] = ToFileUri(root);
			params["rootPath"] = Core::ToUtf8(root);
			params["capabilities"] = std::move(capabilities);

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
		auto [id, bytes] = m_Shared->Rpc.Request("initialize", InitializeParams(root));

		m_Shared->InitializeId = id;
		m_Shared->HandshakeDeadline = std::chrono::steady_clock::now() + kHandshakeTimeout;

		SendNowLocked(*m_Shared, std::move(bytes));
	}

	LspClient::LspClient(const ServerConfig& config, std::filesystem::path binary,
						 std::filesystem::path root)
	{
		m_Shared->ProtocolLanguageId = std::string(config.ProtocolLanguageId);
		m_Shared->NoCompileDatabase = LacksCompileDatabase(root);

		m_Shared->Status.Name = std::string(config.Command);
		m_Shared->Status.Detail = Core::ToUtf8(binary);
		m_Shared->Status.State = ServerState::Starting;

		// --pch-storage=disk keeps preambles out of memory, which is what makes several
		// background files affordable at once: a preamble is the bulk of what a translation
		// unit costs, and the background pass holds more than one open at a time.
		Core::Result<Core::Scope<Core::ChildProcess>> started =
			Core::ChildProcess::Start(binary, {"--background-index", "--pch-storage=disk"}, root);

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

		m_Shared->Process = std::shared_ptr<Core::ChildProcess>(std::move(started.GetValue()));

		m_Reader = std::thread(&LspClient::RunReader, m_Shared);
		m_Writer = std::thread(&LspClient::RunWriter, m_Shared);

		BeginHandshake(root);
	}

	LspClient::LspClient(const ServerConfig& config, std::filesystem::path root, WithoutServer)
	{
		m_Shared->ProtocolLanguageId = std::string(config.ProtocolLanguageId);
		m_Shared->NoCompileDatabase = LacksCompileDatabase(root);

		m_Shared->Status.Name = std::string(config.Command);
		m_Shared->Status.State = ServerState::Starting;

		// Everything the real constructor does except start a process and the two threads that
		// serve it. The handshake still goes out, into a queue nobody drains, so a test can read
		// what would have been sent and answer it through Deliver.
		BeginHandshake(root);
	}

	LspClient::~LspClient()
	{
		m_Shared->Stopping.store(true);

		// Killing the child is what releases both threads at once: it closes the pipe the reader
		// is blocked reading, and it breaks the pipe the writer may be blocked writing into.
		// Asking the server to shut down politely and waiting for it to agree would hold the UI
		// thread here for as long as a wedged server felt like, and a language server holds
		// nothing that is not rebuildable from the files on disk.
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
											const std::string& text)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		Document& document = m_Shared->Documents[uri];

		// Continues from the last version this client ever sent for this URI rather than
		// restarting at 1: a reopen after a close is still this same client's numbering, and a
		// publication that was already in flight for the pre-close version must not be read as
		// answering a version this reopen has not reached yet (see LastVersions' own comment).
		int& lastVersion = m_Shared->LastVersions[uri];
		++lastVersion;
		document.Version = lastVersion;

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["languageId"] = m_Shared->ProtocolLanguageId;
		params["textDocument"]["version"] = document.Version;
		params["textDocument"]["text"] = text;

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didOpen", params));

		return DocumentVersion{document.Version};
	}

	DocumentVersion LspClient::ChangeDocument(const std::filesystem::path& path,
											  const std::string& text)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		const auto found = m_Shared->Documents.find(uri);

		if (found == m_Shared->Documents.end())
		{
			// A change to a document the server was never told about would be a change to nothing.
			return DocumentVersion{};
		}

		Document& document = found->second;
		++document.Version;

		// Full text, not a range. An incremental change describes an edit the server applies to
		// its own copy, and a range computed even slightly wrong corrupts that copy silently —
		// every answer afterwards is subtly about a file nobody has. The text of one source file
		// costs nothing next to that.
		nlohmann::json change;
		change["text"] = text;

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["version"] = document.Version;
		params["contentChanges"] = nlohmann::json::array({std::move(change)});

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didChange", params));

		return DocumentVersion{document.Version};
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

		if (m_Shared->Documents.erase(uri) == 0)
		{
			return;
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

	void LspClient::RequestShutdown()
	{
		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		// Numbered and queued like any request, then immediately forgotten: nothing here will
		// ever read the reply, and leaving the id outstanding would only make JsonRpc's table
		// grow for an object about to be destroyed anyway.
		auto [id, bytes] = m_Shared->Rpc.Request("shutdown", nullptr);
		m_Shared->Rpc.Forget(id);
		SendLocked(*m_Shared, std::move(bytes));

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("exit", nullptr));
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

	void LspClient::RunWriter(std::shared_ptr<Shared> shared)
	{
		Core::ChildProcess& process = *shared->Process;

		for (;;)
		{
			std::vector<std::string> batch;

			{
				std::unique_lock<std::mutex> outbound(shared->OutboundMutex);

				shared->Outbound.wait(outbound,
									  [&shared]
									  {
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

	void LspClient::RunReader(std::shared_ptr<Shared> shared)
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

		// window/logMessage and telemetry still have nowhere to go, and dropping a notification
		// is always allowed.
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

		bool open = false;

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);
			open = shared.Documents.contains(uri);
		}

		// ProtocolMutex is released before StateMutex is taken, deliberately - holding both at
		// once, nested with the lock CloseDocument takes, is the deadlock this file's whole
		// locking order exists to avoid. The cost is a window: a CloseDocument for this same path
		// can land between the check above and the push below, so a publication for a document
		// that is closing gets through anyway. The next update then carries both a Closed note
		// and this publication, and DiagnosticsModel::Apply's fixed close-then-publish order
		// erases the entry the close names and recreates it from the publication - no later
		// Closed can ever arrive to erase it again, because CloseDocument has already taken this
		// path out of Documents. Both readers filter on Core::DocumentSet::Find, so the
		// resurrected entry is invisible to either one: a bounded leak in the model, never a
		// wrong answer shown to anyone.
		if (open)
		{
			const std::lock_guard<std::mutex> state(shared.StateMutex);
			shared.Diagnostics.push_back(std::move(published));
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

		// Parse, correlate, hand over. The columns stay in the protocol's UTF-16 units, and the
		// caller converts them against the buffer it is about to move the caret in — which is the
		// only copy of the file that is certainly the one the user is looking at.
		Answer answer = ReadAnswer(asked, reply, shared.SemanticLegend);

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

		answer.Locations = ParseLocations(reply.Payload);

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

		return answer;
	}

	void LspClient::AnswerServerRequest(Shared& shared, const IncomingMessage& request)
	{
		const RequestId id = *request.RespondTo;

		if (request.Method == "workspace/configuration")
		{
			// One settings object per item asked about, each empty: every setting this side has
			// an opinion about is already on the command line. Answering nothing at all is what
			// stalls clangd — it holds its progress reporting until this returns.
			std::size_t items = 1;

			if (request.Payload.is_object())
			{
				const auto found = request.Payload.find("items");

				if (found != request.Payload.end() && found->is_array() && !found->empty())
				{
					items = found->size();
				}
			}

			nlohmann::json result = nlohmann::json::array();

			for (std::size_t index = 0; index < items; ++index)
			{
				result.push_back(nlohmann::json::object());
			}

			SendNowLocked(shared, shared.Rpc.Reply(id, result));
			return;
		}

		if (request.Method == "window/workDoneProgress/create")
		{
			// Null is the whole of the answer: it means "yes, report progress under that token",
			// and the $/progress notifications the status percentage is read from follow it.
			SendNowLocked(shared, shared.Rpc.Reply(id, nullptr));
			return;
		}

		// Anything else is refused rather than ignored. A server that asked and heard nothing
		// waits; a server told it asked for something unknown carries on.
		SendNowLocked(shared, shared.Rpc.ErrorReply(id, -32601, "method not found"));
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

		// Everything the caller asked for while the handshake was in flight, in the order it was
		// asked, now that the server will accept it.
		for (std::string& queued : shared.Queued)
		{
			SendNowLocked(shared, std::move(queued));
		}

		shared.Queued.clear();

		const std::lock_guard<std::mutex> state(shared.StateMutex);

		shared.Status.State = SettledState(shared.NoCompileDatabase);

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

		const std::lock_guard<std::mutex> state(shared.StateMutex);

		if (stage == "begin")
		{
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
			shared.Status.State = SettledState(shared.NoCompileDatabase);
			shared.Status.Percent = -1;
		}
	}
}

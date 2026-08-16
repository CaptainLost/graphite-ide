#include "Lang/Server/LspClient.h"

#include "Core/Fs/FileSystem.h"
#include "Lang/Server/MessageStream.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		// Everything a URI may carry unescaped. The separator and the drive colon are kept as
		// they are because a server reads them structurally; everything else that is not in
		// RFC 3986's unreserved set is escaped, so the same path always produces the same bytes.
		bool IsUnreservedInPath(char character)
		{
			const unsigned char byte = static_cast<unsigned char>(character);

			if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
				(byte >= '0' && byte <= '9'))
			{
				return true;
			}

			switch (character)
			{
			case '-':
			case '.':
			case '_':
			case '~':
			case '/':
			case ':':
				return true;
			default:
				return false;
			}
		}

		char HexDigit(unsigned value)
		{
			return static_cast<char>(value < 10 ? '0' + value : 'A' + (value - 10));
		}

		std::optional<unsigned> HexValue(char character)
		{
			if (character >= '0' && character <= '9')
			{
				return static_cast<unsigned>(character - '0');
			}

			if (character >= 'a' && character <= 'f')
			{
				return static_cast<unsigned>(character - 'a') + 10;
			}

			if (character >= 'A' && character <= 'F')
			{
				return static_cast<unsigned>(character - 'A') + 10;
			}

			return std::nullopt;
		}

		// How many bytes the code point starting at this lead byte occupies. A continuation byte
		// or anything malformed answers one, so a walk over a broken string always advances.
		std::size_t SequenceLength(unsigned char lead)
		{
			if (lead < 0x80)
			{
				return 1;
			}

			if ((lead & 0xE0) == 0xC0)
			{
				return 2;
			}

			if ((lead & 0xF0) == 0xE0)
			{
				return 3;
			}

			if ((lead & 0xF8) == 0xF0)
			{
				return 4;
			}

			return 1;
		}

		// A four-byte code point is at or above U+10000, which UTF-16 spells as a surrogate pair.
		std::size_t Utf16UnitsFor(std::size_t sequenceLength)
		{
			return sequenceLength == 4 ? 2 : 1;
		}

		std::size_t ReadUnsigned(const nlohmann::json& node, std::string_view key)
		{
			if (!node.is_object())
			{
				return 0;
			}

			const auto found = node.find(std::string(key));

			if (found == node.end())
			{
				return 0;
			}

			const nlohmann::json& value = *found;

			if (!value.is_number_integer() || value.get<std::int64_t>() < 0)
			{
				return 0;
			}

			return static_cast<std::size_t>(value.get<std::int64_t>());
		}

		// The first of `keys` this object carries, or null. Servers spell the same thing several
		// ways — a definition is a Location or a LocationLink, a symbol a DocumentSymbol or a
		// SymbolInformation — and the difference is only which key holds the value.
		const nlohmann::json* FirstOf(const nlohmann::json& node,
									  std::initializer_list<std::string_view> keys)
		{
			if (!node.is_object())
			{
				return nullptr;
			}

			for (const std::string_view key : keys)
			{
				const auto found = node.find(std::string(key));

				if (found != node.end() && !found->is_null())
				{
					return &(*found);
				}
			}

			return nullptr;
		}

		// The "start" of whichever range this node carries, wherever it carries it.
		const nlohmann::json* StartOf(const nlohmann::json& node)
		{
			const nlohmann::json* range =
				FirstOf(node, {"range", "targetSelectionRange", "targetRange"});

			if (range == nullptr)
			{
				const nlohmann::json* location = FirstOf(node, {"location"});

				if (location == nullptr)
				{
					return nullptr;
				}

				range = FirstOf(*location, {"range"});
			}

			if (range == nullptr)
			{
				return nullptr;
			}

			return FirstOf(*range, {"start"});
		}

		std::optional<Location> ReadLocation(const nlohmann::json& node)
		{
			const nlohmann::json* uri = FirstOf(node, {"uri", "targetUri"});

			if (uri == nullptr || !uri->is_string())
			{
				return std::nullopt;
			}

			Location location;
			location.Path = FromFileUri(uri->get<std::string>());

			if (const nlohmann::json* start = StartOf(node))
			{
				location.Line = ReadUnsigned(*start, "line");
				location.Column = Utf16Column{ReadUnsigned(*start, "character")};
			}

			return location;
		}

		std::optional<DeclarationKind> KindFor(std::int64_t symbolKind)
		{
			switch (symbolKind)
			{
			case 6:  // Method
			case 9:  // Constructor
			case 12: // Function
				return DeclarationKind::Function;
			case 5:  // Class
			case 11: // Interface
			case 23: // Struct
				return DeclarationKind::Type;
			default:
				// A variable, a namespace, a field: nothing this editor puts a lens over. Skipped
				// rather than stored as Other, so no caller has to filter the list again.
				return std::nullopt;
			}
		}

		void CollectSymbols(const nlohmann::json& node, std::vector<Declaration>& declarations)
		{
			if (node.is_array())
			{
				for (const nlohmann::json& child : node)
				{
					CollectSymbols(child, declarations);
				}

				return;
			}

			if (!node.is_object())
			{
				return;
			}

			const nlohmann::json* name = FirstOf(node, {"name"});
			const nlohmann::json* kind = FirstOf(node, {"kind"});

			if (name != nullptr && name->is_string() && kind != nullptr && kind->is_number_integer())
			{
				if (const std::optional<DeclarationKind> mapped = KindFor(kind->get<std::int64_t>()))
				{
					Declaration declaration;
					declaration.Name = name->get<std::string>();
					declaration.Kind = *mapped;

					if (const nlohmann::json* start = StartOf(node))
					{
						declaration.Line = ReadUnsigned(*start, "line");
						declaration.Column = Utf16Column{ReadUnsigned(*start, "character")};
					}

					// Before its children, so the flattened list reads in the order the file does.
					declarations.push_back(std::move(declaration));
				}
			}

			// Recursed into whether or not this node itself was kept: a namespace is skipped, and
			// the functions inside it are not.
			if (const nlohmann::json* children = FirstOf(node, {"children"}))
			{
				CollectSymbols(*children, declarations);
			}
		}
	}

	std::string ToFileUri(const std::filesystem::path& path)
	{
		// ToUtf8 rather than string(): on MSVC the latter converts through the active code page,
		// so a path with a non-ASCII character in it would reach the server as mojibake.
		std::string text = Core::ToUtf8(path);

		for (char& character : text)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		std::string uri = "file:///";

		// A POSIX path already starts with the separator the prefix ends in; a Windows one starts
		// with its drive letter and needs all three slashes kept.
		const std::size_t start = (!text.empty() && text.front() == '/') ? 1u : 0u;

		for (std::size_t index = start; index < text.size(); ++index)
		{
			const char character = text[index];

			if (IsUnreservedInPath(character))
			{
				uri.push_back(character);
				continue;
			}

			const unsigned byte = static_cast<unsigned char>(character);

			uri.push_back('%');
			uri.push_back(HexDigit(byte >> 4));
			uri.push_back(HexDigit(byte & 0x0Fu));
		}

		return uri;
	}

	std::filesystem::path FromFileUri(std::string_view uri)
	{
		constexpr std::string_view kScheme = "file://";

		if (uri.starts_with(kScheme))
		{
			uri.remove_prefix(kScheme.size());
		}

		std::string decoded;
		decoded.reserve(uri.size());

		for (std::size_t index = 0; index < uri.size(); ++index)
		{
			if (uri[index] == '%' && index + 2 < uri.size())
			{
				const std::optional<unsigned> high = HexValue(uri[index + 1]);
				const std::optional<unsigned> low = HexValue(uri[index + 2]);

				if (high && low)
				{
					decoded.push_back(static_cast<char>((*high << 4) | *low));
					index += 2;
					continue;
				}
			}

			decoded.push_back(uri[index]);
		}

		// "/D:/a/b" is the same file as "D:/a/b" and only one of the two spellings is a path this
		// filesystem accepts. A leading slash before anything that is not a drive letter belongs
		// to the path and stays.
		if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':' &&
			std::isalpha(static_cast<unsigned char>(decoded[1])) != 0)
		{
			decoded.erase(0, 1);
		}

		return Core::FromUtf8(decoded);
	}

	Utf16Column ToUtf16Column(std::string_view line, std::size_t byteColumn)
	{
		const std::size_t limit = byteColumn < line.size() ? byteColumn : line.size();

		std::size_t units = 0;
		std::size_t index = 0;

		while (index < limit)
		{
			const std::size_t length = SequenceLength(static_cast<unsigned char>(line[index]));

			units += Utf16UnitsFor(length);
			index += length;
		}

		return Utf16Column{units};
	}

	std::size_t FromUtf16Column(std::string_view line, Utf16Column column)
	{
		std::size_t units = 0;
		std::size_t index = 0;

		while (index < line.size() && units < column.Value)
		{
			const std::size_t length = SequenceLength(static_cast<unsigned char>(line[index]));

			units += Utf16UnitsFor(length);
			index += length;
		}

		return index < line.size() ? index : line.size();
	}

	std::vector<Location> ParseLocations(const nlohmann::json& result)
	{
		std::vector<Location> locations;

		if (result.is_array())
		{
			for (const nlohmann::json& node : result)
			{
				if (std::optional<Location> location = ReadLocation(node))
				{
					locations.push_back(std::move(*location));
				}
			}

			return locations;
		}

		// A server answering a single definition is allowed to send the location bare rather than
		// in a one-element array.
		if (std::optional<Location> location = ReadLocation(result))
		{
			locations.push_back(std::move(*location));
		}

		return locations;
	}

	std::vector<Declaration> ParseSymbols(const nlohmann::json& result)
	{
		std::vector<Declaration> declarations;
		CollectSymbols(result, declarations);
		return declarations;
	}

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

	void LspClient::SendNowLocked(Shared& shared, const std::string& bytes)
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

			shared.ToSend.push_back(bytes);
		}

		shared.Outbound.notify_one();
	}

	void LspClient::SendLocked(Shared& shared, const std::string& bytes)
	{
		if (!shared.Initialized)
		{
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
			shared.Queued.push_back(bytes);
			return;
		}

		SendNowLocked(shared, bytes);
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
		SendNowLocked(*m_Shared, bytes);
	}

	LspClient::LspClient(const ServerConfig& config, std::filesystem::path binary,
						 std::filesystem::path root)
	{
		m_Shared->ProtocolLanguageId = std::string(config.ProtocolLanguageId);
		m_Shared->NoCompileDatabase = LacksCompileDatabase(root);

		m_Shared->Status.Name = std::string(config.Command);
		m_Shared->Status.Detail = Core::ToUtf8(binary);
		m_Shared->Status.State = ServerState::Starting;

		Core::Result<Core::Scope<Core::ChildProcess>> started =
			Core::ChildProcess::Start(binary, {"--background-index"}, root);

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

	void LspClient::OpenDocument(const std::filesystem::path& path, const std::string& text)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		Document& document = m_Shared->Documents[uri];
		document.Version = 1;

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["textDocument"]["languageId"] = m_Shared->ProtocolLanguageId;
		params["textDocument"]["version"] = document.Version;
		params["textDocument"]["text"] = text;

		SendLocked(*m_Shared, m_Shared->Rpc.Notification("textDocument/didOpen", params));
	}

	void LspClient::ChangeDocument(const std::filesystem::path& path, const std::string& text)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		const auto found = m_Shared->Documents.find(uri);

		if (found == m_Shared->Documents.end())
		{
			// A change to a document the server was never told about would be a change to nothing.
			return;
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
							 Question question)
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

		m_Shared->Pending[id] = Asked{question};
		SendLocked(*m_Shared, bytes);

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
		params["context"]["includeDeclaration"] = true;

		return Ask("textDocument/references", params, Question::References);
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

	void LspClient::Drain(std::vector<Answer>& answers, ServerStatus& status)
	{
		// The only place anything crosses to the UI thread, and it touches nothing the server can
		// hold up: StateMutex is never held across a read from, or a write to, the pipe.
		const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

		answers.insert(answers.end(), std::make_move_iterator(m_Shared->Answers.begin()),
					   std::make_move_iterator(m_Shared->Answers.end()));

		m_Shared->Answers.clear();

		status = m_Shared->Status;
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
			if (incoming->Method == "$/progress")
			{
				ApplyProgress(shared, incoming->Payload);
			}

			// Everything else a server volunteers — diagnostics, log lines, telemetry — has
			// nowhere to go yet, and dropping a notification is always allowed.
			return;
		}

		if (!incoming->ReplyTo)
		{
			return;
		}

		Asked asked;

		{
			const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

			const auto found = shared.Pending.find(*incoming->ReplyTo);

			if (found == shared.Pending.end())
			{
				// Cancelled while it was in flight; the caller has moved on.
				return;
			}

			asked = found->second;
			shared.Pending.erase(found);
		}

		Answer answer;
		answer.Id = *incoming->ReplyTo;
		answer.Failed = incoming->IsError;

		if (!answer.Failed)
		{
			if (asked.Kind == Question::Symbols)
			{
				answer.Declarations = ParseSymbols(incoming->Payload);
			}
			else
			{
				answer.Locations = ParseLocations(incoming->Payload);
			}
		}

		// Parse, correlate, hand over. The columns stay in the protocol's UTF-16 units, and the
		// caller converts them against the buffer it is about to move the caret in — which is the
		// only copy of the file that is certainly the one the user is looking at.
		const std::lock_guard<std::mutex> state(shared.StateMutex);
		shared.Answers.push_back(std::move(answer));
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
			const std::lock_guard<std::mutex> state(shared.StateMutex);

			shared.Status.State = ServerState::Failed;
			shared.Status.Detail = DescribeError(reply.Payload);
			shared.Status.Percent = -1;

			return;
		}

		shared.Initialized = true;

		const nlohmann::json& result = reply.Payload;

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
		for (const std::string& queued : shared.Queued)
		{
			SendNowLocked(shared, queued);
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

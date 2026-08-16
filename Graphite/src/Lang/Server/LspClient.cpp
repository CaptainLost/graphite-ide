#include "Lang/Server/LspClient.h"

#include "Core/Fs/FileSystem.h"
#include "Lang/Server/MessageStream.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
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
				location.Column = ReadUnsigned(*start, "character");
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
						declaration.Column = ReadUnsigned(*start, "character");
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

		std::string_view LineAt(std::string_view text, std::size_t line)
		{
			std::size_t start = 0;

			for (std::size_t index = 0; index < line; ++index)
			{
				const std::size_t newline = text.find('\n', start);

				if (newline == std::string_view::npos)
				{
					return {};
				}

				start = newline + 1;
			}

			std::size_t end = text.find('\n', start);

			if (end == std::string_view::npos)
			{
				end = text.size();
			}

			if (end > start && text[end - 1] == '\r')
			{
				--end;
			}

			return text.substr(start, end - start);
		}

		// A file whose column has to be converted but which no tab holds open. The cap is there
		// because this runs on the reader thread for every location in an answer, and a reference
		// list can name hundreds of files.
		constexpr std::uintmax_t kLargestFileToMeasure = 8u * 1024u * 1024u;

		std::string ReadForMeasuring(const std::filesystem::path& path)
		{
			std::error_code ec;
			const std::uintmax_t size = std::filesystem::file_size(path, ec);

			if (ec || size > kLargestFileToMeasure)
			{
				return {};
			}

			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return {};
			}

			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

	std::size_t ToUtf16Column(std::string_view line, std::size_t byteColumn)
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

		return units;
	}

	std::size_t FromUtf16Column(std::string_view line, std::size_t utf16Column)
	{
		std::size_t units = 0;
		std::size_t index = 0;

		while (index < line.size() && units < utf16Column)
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
	}

	void LspClient::SendNowLocked(Shared& shared, const std::string& bytes)
	{
		if (!shared.Process)
		{
			return;
		}

		shared.Process->Write(bytes);
	}

	void LspClient::SendLocked(Shared& shared, const std::string& bytes)
	{
		if (!shared.Initialized)
		{
			// Nothing but the handshake may go out before initialize is answered: a server is
			// entitled to drop, or fail on, anything that arrives first. The queue keeps the
			// order the caller asked in.
			shared.Queued.push_back(bytes);
			return;
		}

		SendNowLocked(shared, bytes);
	}

	std::size_t LspClient::ColumnFor(Shared& shared, const std::string& uri,
									 const FilePosition& where)
	{
		const auto found = shared.Documents.find(uri);

		if (found == shared.Documents.end())
		{
			// Nothing to measure against. A byte column and a UTF-16 column agree on every line
			// that is pure ASCII, which is what a position in a document no tab holds falls back
			// to — and asking about such a document is already outside what this client tracks.
			return where.Column;
		}

		return ToUtf16Column(LineAt(found->second.Text, where.Line), where.Column);
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
			return;
		}

		m_Shared->Process =
			std::shared_ptr<Core::ChildProcess>(std::move(started.GetValue()));

		m_Reader = std::thread(&LspClient::RunReader, m_Shared);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		// Recorded before it is sent, and under the same mutex the reader takes to read it, so the
		// reply can never arrive at a reader that does not yet know which id the handshake used.
		auto [id, bytes] = m_Shared->Rpc.Request("initialize", InitializeParams(root));

		m_Shared->InitializeId = id;
		SendNowLocked(*m_Shared, bytes);
	}

	LspClient::~LspClient()
	{
		m_Shared->Stopping.store(true);

		// Killing the child is what closes the pipe the reader is blocked on, and it is the only
		// thing that bounds this wait: asking the server to shut down politely and waiting for it
		// to agree would hold the UI thread here for as long as a wedged server felt like. A
		// language server holds nothing that is not rebuildable from the files on disk.
		if (m_Shared->Process)
		{
			m_Shared->Process->Terminate();
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
		document.Text = text;

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
		document.Text = text;

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
							 Question question, const std::string& uri)
	{
		auto [id, bytes] = m_Shared->Rpc.Request(method, params);

		if (!m_Shared->Process)
		{
			// There is no server to answer, so the answer is written here rather than never: a
			// caller waiting on a ticket that can never come back waits forever.
			const std::lock_guard<std::mutex> state(m_Shared->StateMutex);

			Answer answer;
			answer.Id = id;
			answer.Failed = true;

			m_Shared->Answers.push_back(std::move(answer));

			return id;
		}

		m_Shared->Pending[id] = Asked{question, uri};
		SendLocked(*m_Shared, bytes);

		return id;
	}

	RequestId LspClient::AskDefinition(const FilePosition& where)
	{
		const std::string uri = ToFileUri(where.Path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["position"]["line"] = where.Line;
		params["position"]["character"] = ColumnFor(*m_Shared, uri, where);

		return Ask("textDocument/definition", params, Question::Definition, uri);
	}

	RequestId LspClient::AskReferences(const FilePosition& where)
	{
		const std::string uri = ToFileUri(where.Path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;
		params["position"]["line"] = where.Line;
		params["position"]["character"] = ColumnFor(*m_Shared, uri, where);
		params["context"]["includeDeclaration"] = true;

		return Ask("textDocument/references", params, Question::References, uri);
	}

	RequestId LspClient::AskSymbols(const std::filesystem::path& path)
	{
		const std::string uri = ToFileUri(path);

		const std::lock_guard<std::mutex> protocol(m_Shared->ProtocolMutex);

		nlohmann::json params;
		params["textDocument"]["uri"] = uri;

		return Ask("textDocument/documentSymbol", params, Question::Symbols, uri);
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

	void LspClient::RunReader(std::shared_ptr<Shared> shared)
	{
		MessageStream stream;
		std::array<char, 8192> buffer{};

		// Read without the protocol mutex: it blocks for as long as the server stays quiet, and
		// holding the mutex across that would stop every UI-thread call dead. The pointer itself
		// is set before this thread starts and never reassigned.
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
		}

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
				CompleteHandshake(shared, incoming->Payload);
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

		ToByteColumns(shared, answer, asked.Uri);

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

	void LspClient::CompleteHandshake(Shared& shared, const nlohmann::json& result)
	{
		shared.Initialized = true;

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

	void LspClient::ToByteColumns(Shared& shared, Answer& answer, const std::string& askedUri)
	{
		// One text per file, because a reference list names the same handful of files over and
		// over and each miss otherwise costs a read from disk.
		std::unordered_map<std::string, std::string> texts;

		const auto textFor = [&shared, &texts](const std::string& uri) -> const std::string&
		{
			const auto cached = texts.find(uri);

			if (cached != texts.end())
			{
				return cached->second;
			}

			std::string text;

			{
				const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);

				const auto open = shared.Documents.find(uri);

				if (open != shared.Documents.end())
				{
					text = open->second.Text;
				}
			}

			if (text.empty())
			{
				// The answer names a file no tab holds. Its bytes are the only thing a UTF-16
				// column can be measured against, and an unreadable one simply leaves the column
				// as the server counted it.
				text = ReadForMeasuring(FromFileUri(uri));
			}

			return texts.emplace(uri, std::move(text)).first->second;
		};

		for (Location& location : answer.Locations)
		{
			if (location.Column == 0)
			{
				continue;
			}

			const std::string& text = textFor(ToFileUri(location.Path));

			if (!text.empty())
			{
				location.Column = FromUtf16Column(LineAt(text, location.Line), location.Column);
			}
		}

		if (answer.Declarations.empty())
		{
			return;
		}

		const std::string& text = textFor(askedUri);

		if (text.empty())
		{
			return;
		}

		for (Declaration& declaration : answer.Declarations)
		{
			if (declaration.Column == 0)
			{
				continue;
			}

			declaration.Column = FromUtf16Column(LineAt(text, declaration.Line), declaration.Column);
		}
	}
}

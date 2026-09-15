#include "Lang/Server/LspClient.h"

#include "Core/Fs/FileSystem.h"
#include "Lang/Server/MessageStream.h"

#include <algorithm>
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

		// Whichever range this node carries, wherever it carries it.
		const nlohmann::json* RangeOf(const nlohmann::json& node)
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

			return range;
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

			if (const nlohmann::json* range = RangeOf(node))
			{
				if (const nlohmann::json* start = FirstOf(*range, {"start"}))
				{
					location.Line = ReadUnsigned(*start, "line");
					location.Column = Utf16Column{ReadUnsigned(*start, "character")};
				}

				// Read as well as the start, and left at the default when the server sends none:
				// the end is what tells a caret sitting in the middle of a name apart from one
				// somewhere else on the same line, and a definition's name is the one place a
				// caret pressing F12 usually is. See ResolveDefinition, which is the only reader.
				if (const nlohmann::json* end = FirstOf(*range, {"end"}))
				{
					location.EndLine = ReadUnsigned(*end, "line");
					location.EndColumn = Utf16Column{ReadUnsigned(*end, "character")};
				}
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

					if (const nlohmann::json* range = RangeOf(node))
					{
						if (const nlohmann::json* start = FirstOf(*range, {"start"}))
						{
							declaration.Line = ReadUnsigned(*start, "line");
							declaration.Column = Utf16Column{ReadUnsigned(*start, "character")};
						}
					}

					// The name's own position, which is what the server must be asked about.
					// RangeOf deliberately does not look here: it serves definition and reference
					// replies too, where "selectionRange" means nothing. A flat SymbolInformation
					// carries no selectionRange at all, so the declaration's start stands in —
					// less precise, but a position that can still be asked about.
					declaration.NameLine = declaration.Line;
					declaration.NameColumn = declaration.Column;

					if (const nlohmann::json* selection = FirstOf(node, {"selectionRange"}))
					{
						if (const nlohmann::json* start = FirstOf(*selection, {"start"}))
						{
							declaration.NameLine = ReadUnsigned(*start, "line");
							declaration.NameColumn = Utf16Column{ReadUnsigned(*start, "character")};
						}
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

	namespace {

		// The protocol's own token-type names, mapped onto the far smaller set this editor
		// colours. Anything unlisted is Other, which leaves the lexer's colour standing — the
		// right answer for a name whose kind makes no visible difference.
		SemanticKind KindForTokenType(std::string_view name)
		{
			if (name == "namespace")
			{
				return SemanticKind::Namespace;
			}

			// Every way a server can say "this names a type". clangd sends class, enum, struct
			// and concept where another server might send only "type", and a reader wants them
			// coloured alike regardless.
			if (name == "type" || name == "class" || name == "enum" || name == "struct" ||
				name == "interface" || name == "typeParameter" || name == "concept")
			{
				return SemanticKind::Type;
			}

			if (name == "function" || name == "method")
			{
				return SemanticKind::Function;
			}

			if (name == "macro")
			{
				return SemanticKind::Macro;
			}

			if (name == "property" || name == "field" || name == "enumMember")
			{
				return SemanticKind::Member;
			}

			if (name == "parameter")
			{
				return SemanticKind::Parameter;
			}

			return SemanticKind::Other;
		}
	}

	std::vector<SemanticKind> ParseSemanticLegend(const nlohmann::json& initializeResult)
	{
		std::vector<SemanticKind> legend;

		const nlohmann::json* capabilities = FirstOf(initializeResult, {"capabilities"});

		if (capabilities == nullptr)
		{
			return legend;
		}

		const nlohmann::json* provider = FirstOf(*capabilities, {"semanticTokensProvider"});

		if (provider == nullptr)
		{
			return legend;
		}

		const nlohmann::json* names = FirstOf(*provider, {"legend"});

		if (names == nullptr || (names = FirstOf(*names, {"tokenTypes"})) == nullptr ||
			!names->is_array())
		{
			return legend;
		}

		legend.reserve(names->size());

		for (const nlohmann::json& name : *names)
		{
			// Position matters: index N of this vector answers for the server's type N, so an
			// entry whose name is not a string still has to occupy its slot.
			legend.push_back(name.is_string() ? KindForTokenType(name.get<std::string>())
											  : SemanticKind::Other);
		}

		return legend;
	}

	std::vector<SemanticToken> ParseSemanticTokens(const nlohmann::json& result,
												   const std::vector<SemanticKind>& legend)
	{
		std::vector<SemanticToken> tokens;

		const nlohmann::json* data = FirstOf(result, {"data"});

		if (data == nullptr || !data->is_array())
		{
			return tokens;
		}

		std::size_t line = 0;
		std::size_t column = 0;

		// Five at a time, and a trailing remainder is discarded rather than guessed at: half a
		// token is not a token.
		for (std::size_t index = 0; index + 5 <= data->size(); index += 5)
		{
			const auto read = [&data, index](std::size_t offset) -> std::size_t
			{
				const nlohmann::json& value = (*data)[index + offset];

				return value.is_number_unsigned() ? value.get<std::size_t>() : 0;
			};

			const std::size_t deltaLine = read(0);

			// The column is relative to the previous token only while the line has not moved.
			// Missing this reset shifts every token after the first line break, which reads as
			// the colours sliding rather than as a decoding fault.
			column = deltaLine == 0 ? column + read(1) : read(1);
			line += deltaLine;

			const std::size_t typeIndex = read(3);

			if (typeIndex >= legend.size())
			{
				continue;
			}

			if (legend[typeIndex] == SemanticKind::Other)
			{
				// Nothing to paint over the lexer with, so not worth carrying to the editor.
				continue;
			}

			tokens.push_back(SemanticToken{line, Utf16Column{column}, read(2), legend[typeIndex]});
		}

		return tokens;
	}

	std::vector<Declaration> ParseSymbols(const nlohmann::json& result)
	{
		std::vector<Declaration> declarations;
		CollectSymbols(result, declarations);
		return declarations;
	}

	namespace {

		// nullopt means "drop this diagnostic": Information and Hint are real severities the
		// protocol defines, but nothing downstream draws them. Absent is different from either —
		// the field is optional and left to the client — and is read as the louder of the two
		// kept severities, on the same reasoning ParseDiagnostics documents.
		std::optional<DiagnosticSeverity> SeverityOf(const nlohmann::json& diagnostic)
		{
			const nlohmann::json* severity = FirstOf(diagnostic, {"severity"});

			if (severity == nullptr || !severity->is_number_integer())
			{
				return DiagnosticSeverity::Error;
			}

			switch (severity->get<int>())
			{
			case 1: return DiagnosticSeverity::Error;
			case 2: return DiagnosticSeverity::Warning;
			default: return std::nullopt;   // Information, Hint, or something invented
			}
		}
	}

	PublishedDiagnostics ParseDiagnostics(const nlohmann::json& params)
	{
		PublishedDiagnostics published;

		const nlohmann::json* uri = FirstOf(params, {"uri"});

		if (uri == nullptr || !uri->is_string())
		{
			return published;
		}

		published.Path = FromFileUri(uri->get<std::string>());

		if (const nlohmann::json* version = FirstOf(params, {"version"});
			version != nullptr && version->is_number_integer())
		{
			published.Version = DocumentVersion{version->get<int>()};
		}

		const nlohmann::json* items = FirstOf(params, {"diagnostics"});

		if (items == nullptr || !items->is_array())
		{
			return published;
		}

		for (const nlohmann::json& item : *items)
		{
			if (!item.is_object())
			{
				continue;
			}

			const std::optional<DiagnosticSeverity> severity = SeverityOf(item);

			if (!severity)
			{
				continue;
			}

			Diagnostic diagnostic;
			diagnostic.Severity = *severity;

			if (const nlohmann::json* range = FirstOf(item, {"range"}))
			{
				if (const nlohmann::json* start = FirstOf(*range, {"start"}))
				{
					diagnostic.Line = ReadUnsigned(*start, "line");
					diagnostic.Column = Utf16Column{ReadUnsigned(*start, "character")};
				}

				// An end the server did not give collapses onto the start rather than onto
				// (0, 0): a zero-length span at the right place is drawable, one at the top of
				// the file is a squiggle under text nobody complained about.
				if (const nlohmann::json* end = FirstOf(*range, {"end"}))
				{
					diagnostic.EndLine = ReadUnsigned(*end, "line");
					diagnostic.EndColumn = Utf16Column{ReadUnsigned(*end, "character")};
				}
				else
				{
					diagnostic.EndLine = diagnostic.Line;
					diagnostic.EndColumn = diagnostic.Column;
				}
			}

			if (const nlohmann::json* message = FirstOf(item, {"message"});
				message != nullptr && message->is_string())
			{
				diagnostic.Message = message->get<std::string>();
			}

			if (const nlohmann::json* source = FirstOf(item, {"source"});
				source != nullptr && source->is_string())
			{
				diagnostic.Source = source->get<std::string>();
			}

			published.Items.push_back(std::move(diagnostic));
		}

		return published;
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

			if (incoming->Method == "textDocument/publishDiagnostics")
			{
				PublishedDiagnostics published = ParseDiagnostics(incoming->Payload);

				if (!published.Path.empty())
				{
					// Dropped unless this client holds the document open. A publication can
					// outlive its didClose - the notification is already in flight when the
					// close goes out - and the warm-up in LanguageServices::Poll opens and
					// closes a file deliberately. Shared::Documents is the authority on what is
					// open and is already erased on close, so this check costs a lookup.
					const std::string uri = ToFileUri(published.Path);

					bool open = false;

					{
						const std::lock_guard<std::mutex> protocol(shared.ProtocolMutex);
						open = shared.Documents.contains(uri);
					}

					// ProtocolMutex is released before StateMutex is taken, deliberately - holding
					// both at once, nested with the lock CloseDocument takes, is the deadlock this
					// file's whole locking order exists to avoid. The cost is a window: a
					// CloseDocument for this same path can land between the check above and the
					// push below, so a publication for a document that is closing gets through
					// anyway. The next update then carries both a Closed note and this
					// publication, and DiagnosticsModel::Apply's fixed close-then-publish order
					// erases the entry the close names and recreates it from the publication - no
					// later Closed can ever arrive to erase it again, because CloseDocument has
					// already taken this path out of Documents. Both readers filter on
					// Core::DocumentSet::Find, so the resurrected entry is invisible to either one:
					// a bounded leak in the model, never a wrong answer shown to anyone.
					if (open)
					{
						const std::lock_guard<std::mutex> state(shared.StateMutex);
						shared.Diagnostics.push_back(std::move(published));
					}
				}

				return;
			}

			// window/logMessage and telemetry still have nowhere to go, and dropping a
			// notification is always allowed.
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
			else if (asked.Kind == Question::SemanticTokens)
			{
				answer.SemanticTokens = ParseSemanticTokens(incoming->Payload, shared.SemanticLegend);
			}
			else
			{
				answer.Locations = ParseLocations(incoming->Payload);

				// Nobody is shown the line they asked from. The protocol counts a declaration as
				// a reference to itself, so a count drawn over a definition would otherwise
				// include that definition and read one too high, and the list opened from it
				// would lead its results with the row already on screen. Only for references:
				// a definition reply naming the place asked about is the answer, not noise, and
				// ResolveDefinition is what decides there is nowhere to go.
				if (asked.Kind == Question::References && asked.From)
				{
					const FilePosition& from = *asked.From;

					std::erase_if(answer.Locations, [&from](const Location& location) {
						return location.Line == from.Line && location.Column == from.Column &&
							   location.Path == from.Path;
					});
				}
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

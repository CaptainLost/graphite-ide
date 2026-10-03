#include "Language/Server/LspProtocol.h"

#include "Core/FileSystem/FileSystem.h"

#include <cctype>
#include <cstdint>
#include <initializer_list>
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
			case 6:	 // Method
			case 9:	 // Constructor
			case 12: // Function
				return DeclarationKind::Function;
			case 5:	 // Class
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

	std::optional<Location> ParseSwitchSourceHeader(const nlohmann::json& result)
	{
		if (!result.is_string())
		{
			return std::nullopt;
		}

		Location location;
		location.Path = FromFileUri(result.get<std::string>());

		return location;
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
			const auto read = [&data, index](std::size_t offset) -> std::size_t {
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
			case 1:
				return DiagnosticSeverity::Error;
			case 2:
				return DiagnosticSeverity::Warning;
			default:
				return std::nullopt; // Information, Hint, or something invented
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
}

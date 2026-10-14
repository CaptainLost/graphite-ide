#include "Language/Server/LspProtocol.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Base/Ascii.h"
#include "Core/Text/Encoding.h"

#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		constexpr unsigned kDecimalDigitCount = 10;
		constexpr unsigned kBitsPerHexDigit = 4;
		constexpr unsigned kLowHexDigitMask = 0x0F;
		constexpr std::size_t kFourByteSequenceLength = 4;
		constexpr std::size_t kSurrogatePairUnits = 2;
		constexpr std::size_t kSingleUnit = 1;
		constexpr std::string_view kFileUriPrefix = "file:///";

		constexpr std::int64_t kMethodSymbolKind = 6;
		constexpr std::int64_t kConstructorSymbolKind = 9;
		constexpr std::int64_t kFunctionSymbolKind = 12;
		constexpr std::int64_t kClassSymbolKind = 5;
		constexpr std::int64_t kInterfaceSymbolKind = 11;
		constexpr std::int64_t kStructSymbolKind = 23;

		constexpr int kErrorSeverityValue = 1;
		constexpr int kWarningSeverityValue = 2;

		constexpr std::size_t kIntegersPerSemanticToken = 5;
		constexpr std::size_t kDeltaLineField = 0;
		constexpr std::size_t kDeltaColumnField = 1;
		constexpr std::size_t kLengthField = 2;
		constexpr std::size_t kTypeIndexField = 3;

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

		char UppercaseHexDigit(unsigned value)
		{
			return static_cast<char>(value < kDecimalDigitCount
										 ? '0' + value
										 : 'A' + (value - kDecimalDigitCount));
		}

		bool StartsWithDriveLetter(const std::string& path)
		{
			return path.size() >= 2 && path[1] == ':' &&
				   std::isalpha(static_cast<unsigned char>(path[0])) != 0;
		}

		void UppercaseDriveLetter(std::string& path)
		{
			if (StartsWithDriveLetter(path))
			{
				path[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
			}
		}

		std::size_t Utf16UnitsFor(std::size_t sequenceLength)
		{
			return sequenceLength == kFourByteSequenceLength ? kSurrogatePairUnits : kSingleUnit;
		}

		std::size_t ReadUnsigned(const nlohmann::json& node, std::string_view key)
		{
			if (!node.is_object())
			{
				return 0;
			}

			const nlohmann::json::const_iterator found = node.find(std::string(key));

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

		const nlohmann::json* FirstOf(const nlohmann::json& node,
									  std::initializer_list<std::string_view> keys)
		{
			if (!node.is_object())
			{
				return nullptr;
			}

			for (const std::string_view key : keys)
			{
				const nlohmann::json::const_iterator found = node.find(std::string(key));

				if (found != node.end() && !found->is_null())
				{
					return &(*found);
				}
			}

			return nullptr;
		}

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
			case kMethodSymbolKind:
			case kConstructorSymbolKind:
			case kFunctionSymbolKind:
				return DeclarationKind::Function;
			case kClassSymbolKind:
			case kInterfaceSymbolKind:
			case kStructSymbolKind:
				return DeclarationKind::Type;
			default:
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

					declarations.push_back(std::move(declaration));
				}
			}

			if (const nlohmann::json* children = FirstOf(node, {"children"}))
			{
				CollectSymbols(*children, declarations);
			}
		}
	}

	std::string ToFileUri(const std::filesystem::path& path)
	{
		// WARNING: ToUtf8 rather than string(): on MSVC the latter converts through the active
		// code page, so a path with a non-ASCII character would reach the server as mojibake.
		std::string text = Core::ToUtf8(path);

		for (char& character : text)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		UppercaseDriveLetter(text);

		std::string uri{kFileUriPrefix};

		const bool isPosixAbsolute = !text.empty() && text.front() == '/';
		const std::size_t firstEncodedIndex = isPosixAbsolute ? 1u : 0u;

		for (std::size_t characterIndex = firstEncodedIndex; characterIndex < text.size(); ++characterIndex)
		{
			const char character = text[characterIndex];

			if (IsUnreservedInPath(character))
			{
				uri.push_back(character);
				continue;
			}

			const unsigned byte = static_cast<unsigned char>(character);

			uri.push_back('%');
			uri.push_back(UppercaseHexDigit(byte >> kBitsPerHexDigit));
			uri.push_back(UppercaseHexDigit(byte & kLowHexDigitMask));
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

		for (std::size_t characterIndex = 0; characterIndex < uri.size(); ++characterIndex)
		{
			if (uri[characterIndex] == '%' && characterIndex + 2 < uri.size())
			{
				const std::optional<std::uint8_t> highDigit = Core::HexDigitValue(uri[characterIndex + 1]);
				const std::optional<std::uint8_t> lowDigit = Core::HexDigitValue(uri[characterIndex + 2]);

				if (highDigit && lowDigit)
				{
					decoded.push_back(static_cast<char>((*highDigit << kBitsPerHexDigit) | *lowDigit));
					characterIndex += 2;
					continue;
				}
			}

			decoded.push_back(uri[characterIndex]);
		}

		const bool hasSlashBeforeDriveLetter =
			decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':' &&
			std::isalpha(static_cast<unsigned char>(decoded[1])) != 0;

		if (hasSlashBeforeDriveLetter)
		{
			decoded.erase(0, 1);
		}

		UppercaseDriveLetter(decoded);

		return Core::FromUtf8(decoded);
	}

	Utf16Column ToUtf16Column(std::string_view line, std::size_t byteColumn)
	{
		const std::size_t limit = byteColumn < line.size() ? byteColumn : line.size();

		std::size_t units = 0;
		std::size_t byteIndex = 0;

		while (byteIndex < limit)
		{
			const std::size_t sequenceLength = Core::Utf8SequenceLength(line[byteIndex]);

			units += Utf16UnitsFor(sequenceLength);
			byteIndex += sequenceLength;
		}

		return Utf16Column{units};
	}

	std::size_t FromUtf16Column(std::string_view line, Utf16Column column)
	{
		std::size_t units = 0;
		std::size_t byteIndex = 0;

		while (byteIndex < line.size() && units < column.Value)
		{
			const std::size_t sequenceLength = Core::Utf8SequenceLength(line[byteIndex]);

			units += Utf16UnitsFor(sequenceLength);
			byteIndex += sequenceLength;
		}

		return byteIndex < line.size() ? byteIndex : line.size();
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

		if (std::optional<Location> location = ReadLocation(result))
		{
			locations.push_back(std::move(*location));
		}

		return locations;
	}

	namespace {

		bool NamesType(std::string_view tokenTypeName)
		{
			return tokenTypeName == "type" || tokenTypeName == "class" || tokenTypeName == "enum" ||
				   tokenTypeName == "struct" || tokenTypeName == "interface" ||
				   tokenTypeName == "typeParameter" || tokenTypeName == "concept" ||
				   tokenTypeName == "recordClass" || tokenTypeName == "recordStruct" ||
				   tokenTypeName == "delegate" || tokenTypeName == "delegateName";
		}

		bool NamesFunction(std::string_view tokenTypeName)
		{
			return tokenTypeName == "function" || tokenTypeName == "method" ||
				   tokenTypeName == "extensionMethod" || tokenTypeName == "extensionMethodName";
		}

		bool NamesMember(std::string_view tokenTypeName)
		{
			return tokenTypeName == "property" || tokenTypeName == "field" ||
				   tokenTypeName == "fieldName" || tokenTypeName == "constant" ||
				   tokenTypeName == "constantName" || tokenTypeName == "enumMember" ||
				   tokenTypeName == "event";
		}

		SemanticKind KindForTokenType(std::string_view tokenTypeName)
		{
			if (tokenTypeName == "namespace")
			{
				return SemanticKind::Namespace;
			}

			if (NamesType(tokenTypeName))
			{
				return SemanticKind::Type;
			}

			if (NamesFunction(tokenTypeName))
			{
				return SemanticKind::Function;
			}

			if (tokenTypeName == "macro")
			{
				return SemanticKind::Macro;
			}

			if (NamesMember(tokenTypeName))
			{
				return SemanticKind::Member;
			}

			if (tokenTypeName == "parameter")
			{
				return SemanticKind::Parameter;
			}

			return SemanticKind::Other;
		}

		std::size_t ReadTokenField(const nlohmann::json& data, std::size_t tokenStart, std::size_t fieldOffset)
		{
			const nlohmann::json& value = data[tokenStart + fieldOffset];

			return value.is_number_unsigned() ? value.get<std::size_t>() : 0;
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

		const nlohmann::json* legendNode = FirstOf(*provider, {"legend"});

		if (legendNode == nullptr)
		{
			return legend;
		}

		const nlohmann::json* tokenTypes = FirstOf(*legendNode, {"tokenTypes"});

		if (tokenTypes == nullptr || !tokenTypes->is_array())
		{
			return legend;
		}

		legend.reserve(tokenTypes->size());

		for (const nlohmann::json& tokenTypeName : *tokenTypes)
		{
			legend.push_back(tokenTypeName.is_string()
								 ? KindForTokenType(tokenTypeName.get<std::string>())
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

		for (std::size_t tokenStart = 0; tokenStart + kIntegersPerSemanticToken <= data->size();
			 tokenStart += kIntegersPerSemanticToken)
		{
			const std::size_t deltaLine = ReadTokenField(*data, tokenStart, kDeltaLineField);
			const std::size_t deltaColumn = ReadTokenField(*data, tokenStart, kDeltaColumnField);

			column = deltaLine == 0 ? column + deltaColumn : deltaColumn;
			line += deltaLine;

			const std::size_t typeIndex = ReadTokenField(*data, tokenStart, kTypeIndexField);

			if (typeIndex >= legend.size())
			{
				continue;
			}

			if (legend[typeIndex] == SemanticKind::Other)
			{
				continue;
			}

			const std::size_t length = ReadTokenField(*data, tokenStart, kLengthField);

			tokens.push_back(SemanticToken{.Line = line,
										   .Column = Utf16Column{column},
										   .Length = length,
										   .Kind = legend[typeIndex]});
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

		std::optional<DiagnosticSeverity> SeverityOf(const nlohmann::json& diagnostic)
		{
			const nlohmann::json* severity = FirstOf(diagnostic, {"severity"});

			if (severity == nullptr || !severity->is_number_integer())
			{
				return DiagnosticSeverity::Error;
			}

			switch (severity->get<int>())
			{
			case kErrorSeverityValue:
				return DiagnosticSeverity::Error;
			case kWarningSeverityValue:
				return DiagnosticSeverity::Warning;
			default:
				return std::nullopt;
			}
		}

		std::vector<Diagnostic> ParseDiagnosticItems(const nlohmann::json& items)
		{
			std::vector<Diagnostic> diagnostics;

			if (!items.is_array())
			{
				return diagnostics;
			}

			for (const nlohmann::json& item : items)
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

				diagnostics.push_back(std::move(diagnostic));
			}

			return diagnostics;
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

		if (const nlohmann::json* items = FirstOf(params, {"diagnostics"}))
		{
			published.Items = ParseDiagnosticItems(*items);
		}

		return published;
	}

	std::optional<std::vector<Diagnostic>> ParseDiagnosticReport(const nlohmann::json& result)
	{
		const nlohmann::json* kind = FirstOf(result, {"kind"});

		if (kind == nullptr || !kind->is_string() || kind->get<std::string>() != "full")
		{
			return std::nullopt;
		}

		const nlohmann::json* items = FirstOf(result, {"items"});

		return items == nullptr ? std::vector<Diagnostic>{} : ParseDiagnosticItems(*items);
	}

	bool OffersDiagnosticPulls(const nlohmann::json& initializeResult)
	{
		const nlohmann::json* capabilities = FirstOf(initializeResult, {"capabilities"});

		if (capabilities == nullptr)
		{
			return false;
		}

		const nlohmann::json* provider = FirstOf(*capabilities, {"diagnosticProvider"});

		if (provider == nullptr)
		{
			return false;
		}

		const bool isOptionsObject = provider->is_object();
		const bool isEnabledFlag = provider->is_boolean() && provider->get<bool>();

		return isOptionsObject || isEnabledFlag;
	}
}

#pragma once

#include "Language/Api/LanguageId.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	// WARNING: Utf16Column counts UTF-16 code units as the protocol does, while the editor counts
	// bytes; a bare std::size_t must not convert to it, or a missed conversion is silent.
	struct Utf16Column
	{
		// WARNING: Keep the constructor explicit and the type a non-aggregate, or brace elision
		// accepts FilePosition{path, line, 21} with a byte column.
		Utf16Column() = default;
		explicit constexpr Utf16Column(std::size_t value) : Value(value) {}

		std::size_t Value = 0;

		friend bool operator==(const Utf16Column&, const Utf16Column&) = default;
	};

	struct FilePosition
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		Utf16Column Column;

		friend bool operator==(const FilePosition&, const FilePosition&) = default;
	};

	struct Location
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		Utf16Column Column;

		// WARNING: A reply carrying no range leaves the end at (0, 0), an empty span at the start.
		std::size_t EndLine = 0;
		Utf16Column EndColumn;
	};

	enum class DocumentRole : std::uint8_t
	{
		Consulted,
		Shown
	};

	enum class SemanticKind : std::uint8_t
	{
		Type,
		Function,
		Macro,
		Member,
		Parameter,
		Namespace,
		Other
	};

	struct SemanticToken
	{
		std::size_t Line = 0;
		Utf16Column Column;
		std::size_t Length = 0;
		SemanticKind Kind = SemanticKind::Other;
	};

	enum class DiagnosticSeverity : std::uint8_t
	{
		Error,
		Warning
	};

	struct Diagnostic
	{
		std::size_t Line = 0;
		Utf16Column Column;
		std::size_t EndLine = 0;
		Utf16Column EndColumn;
		DiagnosticSeverity Severity = DiagnosticSeverity::Error;
		std::string Message;
		std::string Source;
	};

	struct DocumentVersion
	{
		DocumentVersion() = default;
		explicit constexpr DocumentVersion(int value) : Value(value) {}

		// WARNING: Zero means "never sent"; LspClient stamps a version-less publication with the
		// version last sent, so zero never arrives as an answer but still compares below every real version.
		int Value = 0;

		friend std::strong_ordering operator<=>(const DocumentVersion&, const DocumentVersion&) = default;
	};

	struct PublishedDiagnostics
	{
		std::filesystem::path Path;
		DocumentVersion Version;
		std::vector<Diagnostic> Items;
	};

	struct DocumentVersionNote
	{
		std::filesystem::path Path;
		DocumentVersion Version;
	};

	struct DiagnosticsUpdate
	{
		std::vector<PublishedDiagnostics> Published;
		std::vector<DocumentVersionNote> Versions;
		std::vector<std::filesystem::path> Closed;
	};

	enum class DeclarationKind : std::uint8_t
	{
		Function,
		Type,
		Other
	};

	// WARNING: Line/Column is where the declaration begins and NameLine/NameColumn where its name
	// begins; the server must be asked at the name, or a constructor resolves to its class.
	struct Declaration
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Other;
		std::size_t Line = 0;
		Utf16Column Column;
		std::size_t NameLine = 0;
		Utf16Column NameColumn;
	};

	enum class ServerState : std::uint8_t
	{
		NotConfigured,
		Off,
		NotFound,
		NotInstalled,
		MissingRuntime,
		Starting,
		Indexing,
		ProjectIncomplete,
		Ready,
		Failed,

		Restricted
	};

	struct ProjectProblem
	{
		std::string Summary;
		std::string Explanation;
	};

	inline constexpr int kNoPercentReported = -1;

	struct ServerStatus
	{
		ServerState State = ServerState::NotConfigured;
		std::string Name;
		std::string Detail;
		int Percent = kNoPercentReported;
		std::size_t Restarts = 0;
		ProjectProblem Problem;
		std::string Loading;
		LanguageId Language = LanguageId::PlainText;
	};

	enum class ServerMessageKind : std::uint8_t
	{
		Error,
		Warning
	};

	struct ServerMessage
	{
		std::string Server;
		ServerMessageKind Kind = ServerMessageKind::Error;
		std::string Text;
	};
}

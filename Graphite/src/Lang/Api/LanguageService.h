#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	// The value types the editor and the language-server client speak.
	//
	// The interface this task was drafted to declare (LanguageService, with DefinitionAt,
	// ReferencesTo, SymbolsIn, StatusFor as synchronous virtuals) is deliberately not here.
	// It would have been synchronous over a client that must be asynchronous, because nothing
	// may block the UI thread; a synchronous interface over an asynchronous client can only be
	// implemented by blocking, so it could never have been implemented. The testability that
	// interface was meant to buy is provided instead by the pure functions the editor logic
	// actually goes through, each tested without a server. If a second backend ever arrives,
	// an interface gets extracted then, over a shape that is settled.

	// A column counted the way the protocol counts: in UTF-16 code units. This editor counts
	// bytes, and the two disagree on every line holding a character outside ASCII — silently, and
	// only on those lines, which is exactly the kind of mistake that survives a test suite.
	//
	// It is a type rather than a field name because nothing else can make the mistake loud. The
	// conversion happens at the point of use, with Graphite::ToUtf16Column and
	// Graphite::FromUtf16Column, where the authoritative buffer is in hand; the client cannot do
	// it, having only the file on disk to measure against, which for a document with unsaved
	// edits is a different file. So a missed conversion cannot be caught at runtime by anything —
	// but a std::size_t handed to something wanting one of these does not compile (ADR 0023).
	struct Utf16Column
	{
		// Not an aggregate, and the constructor is explicit, both deliberately. Left an aggregate,
		// brace elision would quietly accept FilePosition{path, line, 21} — the nested braces may
		// be omitted, so a byte column written as a bare number would compile and mean nothing it
		// says. Spelling Utf16Column{21} is the point of the exercise.
		Utf16Column() = default;
		explicit constexpr Utf16Column(std::size_t value) : Value(value) {}

		std::size_t Value = 0;

		friend bool operator==(const Utf16Column&, const Utf16Column&) = default;
	};

	// Line stays a plain std::size_t throughout: a line number means the same thing on both sides
	// of the protocol, so wrapping it would be ceremony with no hazard behind it. Lines are
	// zero-based, matching the protocol; the status bar displays them one-based, so the offset
	// there is deliberate.

	// A position to send to the language server.
	struct FilePosition
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		Utf16Column Column;

		// What lets HoverQuestion, which carries one, be compared with its defaulted operator.
		friend bool operator==(const FilePosition&, const FilePosition&) = default;
	};

	// A position received from the language server.
	struct Location
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		Utf16Column Column;

		// The exclusive end of the range the server named, when it named one. A jump only ever
		// spends the start; the end exists so ResolveDefinition can tell a caret sitting inside
		// the name it is asking about — which is where a caret pressing F12 usually is — from one
		// merely on the same line. Left at (0, 0) by a reply carrying no range, which reads as an
		// empty span at the start and reduces that test to a comparison against the start alone.
		std::size_t EndLine = 0;
		Utf16Column EndColumn;
	};

	// What a server says an identifier *is*, which no lexer can know: it sees `Application`,
	// `s_Instance` and `CORE_ASSERT` as the same kind of word, and only something that has
	// compiled the code can tell a type from a static member from a macro.
	//
	// Deliberately far smaller than the protocol's own list. Every value here is a colour someone
	// has to choose in every scheme, and distinctions nobody can see cost exactly as much to
	// maintain as ones they can. Other means "the lexer's own colour stands" — most identifiers
	// are ordinary variables, and colouring those differently from nothing is not worth a slot.
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

	// One coloured span, in the units the server speaks: a line, a column in UTF-16 code units,
	// and a length in the same. Turned into bytes against the line's own text by whoever draws it.
	struct SemanticToken
	{
		std::size_t Line = 0;
		Utf16Column Column;
		std::size_t Length = 0;
		SemanticKind Kind = SemanticKind::Other;
	};

	// The two severities this editor draws. The protocol names four; Information and Hint are
	// dropped at the parse, so nothing downstream can accidentally grow a case for them - see
	// ParseDiagnostics.
	enum class DiagnosticSeverity : std::uint8_t
	{
		Error,
		Warning
	};

	// What a server said is wrong with one span of one document, in the units the protocol
	// speaks. Turned into the editor's own coordinates by DiagnosticOverlay, which has the
	// buffer and the tab width this cannot see.
	struct Diagnostic
	{
		std::size_t Line = 0;
		Utf16Column Column;
		std::size_t EndLine = 0;
		Utf16Column EndColumn;
		DiagnosticSeverity Severity = DiagnosticSeverity::Error;
		std::string Message;
		std::string Source; // "clang", "clang-tidy" - shown in the tooltip after the message
	};

	// The protocol's own count of how many times a document has been sent, which is the only
	// thing that can say whether an answer describes the text on screen. A type rather than an
	// int for the reason Utf16Column is one: it is trivially confused with Core::TextBuffer's
	// revision counter, which counts a different thing entirely, and nothing but the type system
	// can make that confusion loud.
	struct DocumentVersion
	{
		DocumentVersion() = default;
		explicit constexpr DocumentVersion(int value) : Value(value) {}

		// Zero is "never sent", and what a reply naming no version reads as. It compares less
		// than every real version, so such a reply reads as stale rather than as current.
		int Value = 0;

		// A defaulted three-way comparison declares == alongside it, so both spellings work
		// from one line: IsStale needs <, and every test needs ==.
		friend auto operator<=>(const DocumentVersion&, const DocumentVersion&) = default;
	};

	// One document's diagnostics as a server last published them.
	struct PublishedDiagnostics
	{
		std::filesystem::path Path; // empty when the notification named no usable uri
		DocumentVersion Version;
		std::vector<Diagnostic> Items; // empty means "all clear", which is a real answer
	};

	// What version a document was last sent under. Half of freshness; the other half is the
	// version a publication names.
	struct DocumentVersionNote
	{
		std::filesystem::path Path;
		DocumentVersion Version;
	};

	// What the servers have said about documents since the last call, and what has since been
	// sent to them. Both halves in one call because a set of diagnostics and the version it is
	// measured against are one fact, and delivering them separately is a frame in which they
	// disagree.
	struct DiagnosticsUpdate
	{
		std::vector<PublishedDiagnostics> Published;
		std::vector<DocumentVersionNote> Versions;

		// Documents no server holds open any more - a closed tab, or a restart that threw the
		// old process's numbering away. What makes an entry disappear.
		std::vector<std::filesystem::path> Closed;
	};

	enum class DeclarationKind : std::uint8_t
	{
		Function,
		Type,
		Other
	};

	// Two positions, because the protocol gives two and they answer different questions.
	//
	// Line/Column are where the declaration begins — what the lens is anchored to and indented
	// from, so a count over an indented function lines up with it.
	//
	// NameLine/NameColumn are where its name begins, and are what the server is asked about. The
	// two differ for anything whose declaration starts before its own name, and a constructor is
	// the case that makes the difference visible: `Application::Application(...)` begins with the
	// class's name, so asking at the declaration's start resolves to the class and the lens
	// counts every mention of the type rather than every construction of it.
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
		NotFound,
		Starting,
		Indexing,
		NoCompileDatabase,
		Ready,
		Failed,

		// The root's folder is not trusted: the entry exists, and keeps what documents were
		// sent to it, but no process runs until trust is granted (ADR 0532).
		Restricted
	};

	struct ServerStatus
	{
		ServerState State = ServerState::NotConfigured;
		std::string Name;	// "clangd"
		std::string Detail; // binary path, version, what is missing
		int Percent = -1;	// -1 means no percentage reported; meaningful only while State is Indexing
		std::size_t Restarts = 0;
	};
}

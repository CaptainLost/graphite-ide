#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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

	enum class DeclarationKind : std::uint8_t { Function, Type, Other };

	struct Declaration
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Other;
		std::size_t Line = 0;
		Utf16Column Column;
	};

	enum class ServerState : std::uint8_t
	{
		NotConfigured, NotFound, Starting, Indexing, NoCompileDatabase, Ready, Failed
	};

	struct ServerStatus
	{
		ServerState State = ServerState::NotConfigured;
		std::string Name;              // "clangd"
		std::string Detail;            // binary path, version, what is missing
		int Percent = -1;              // -1 means no percentage reported; meaningful only while State is Indexing
		std::size_t Restarts = 0;
	};
}

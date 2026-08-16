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

	// Every column in this header is a UTF-16 code unit, because that is the unit the protocol
	// counts in and the field names say so. This editor counts bytes, so both directions are
	// converted with Graphite::ToUtf16Column and Graphite::FromUtf16Column — and both are called
	// at the point of use, where the authoritative buffer is already in hand, never inside the
	// client. The client has only the file on disk to measure against, and for a document with
	// unsaved edits that is a different file: the conversion would not merely cost a read, it
	// would silently answer about text the user cannot see. Line is zero-based, matching the
	// protocol; the status bar displays lines one-based, so the offset is deliberate here.

	// A position to send to the language server.
	struct FilePosition
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		std::size_t Utf16Column = 0;
	};

	// A position received from the language server.
	struct Location
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		std::size_t Utf16Column = 0;
	};

	enum class DeclarationKind : std::uint8_t { Function, Type, Other };

	struct Declaration
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Other;
		std::size_t Line = 0;
		std::size_t Utf16Column = 0;
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

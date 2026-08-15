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

	// A position to send to the language server. The protocol counts Column in UTF-16 code
	// units; this editor counts bytes. The conversion happens at the client boundary and
	// nowhere else — do not convert again further in. Line is zero-based, matching the
	// protocol; the status bar displays lines one-based, so the offset is deliberate here.
	struct FilePosition
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		std::size_t Column = 0;
	};

	// A position received from the language server. Same units and the same caveat as
	// FilePosition: Column is UTF-16 code units, converted to bytes at the client boundary;
	// Line is zero-based.
	struct Location
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		std::size_t Column = 0;
	};

	enum class DeclarationKind : std::uint8_t { Function, Type, Other };

	struct Declaration
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Other;
		std::size_t Line = 0;
		std::size_t Column = 0;
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

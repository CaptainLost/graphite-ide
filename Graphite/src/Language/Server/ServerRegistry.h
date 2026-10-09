#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Server/ServerDiscovery.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace Graphite {

	// One row of "which language server serves which language" — the table ADR 0485 promises
	// in place of a class per language. A second language is a second constexpr object beside
	// kCpp in ServerRegistry.cpp and one more case in ServerConfigFor.
	struct ServerConfig
	{
		std::string_view Command;					   // launch command, e.g. "clangd"
		std::string_view ProtocolLanguageId;		   // the LSP languageId this server expects
		std::span<const std::string_view> RootMarkers; // checked nearest-first, see RootFor
	};

	// The config for a language, or null when no server is configured for it yet. Returning a
	// pointer into static storage rather than a value keeps this total and allocation-free.
	const ServerConfig* ServerConfigFor(LanguageId language);

	// Walks upward from `file`'s directory, stopping at the first directory that holds any of
	// `config.RootMarkers`. Proximity beats marker type deliberately: a nearer directory naming
	// a build (compile_commands.json, .clangd) is a better root than a farther one naming only
	// a checkout (.git), so the walk stops at the first marker found regardless of which marker
	// it is, rather than preferring one kind of marker over distance. Returns `fallbackRoot`
	// when nothing above `file` carries a marker.
	std::filesystem::path RootFor(const ServerConfig& config, const std::filesystem::path& file,
								  const std::filesystem::path& fallbackRoot);

	// True when `directory` carries any configured server's root marker — that is, when RootFor
	// would stop there rather than climbing past it.
	//
	// The question a walk asks to find out where somebody else's project begins. A submodule
	// under vendor/ carries its own .git, so every file below it resolves to its own root and
	// would be served by its own language server, started from nothing and usually without a
	// compile database. That is somebody else's code and somebody else's server; a pass that
	// reads ahead for the reader has no business in it.
	bool IsProjectRoot(const std::filesystem::path& directory);

	// True when `root` holds none of the forms clangd accepts as a compile database:
	// compile_commands.json, compile_flags.txt, .clangd.
	bool LacksCompileDatabase(const std::filesystem::path& root);
}

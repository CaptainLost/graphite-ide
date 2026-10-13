#pragma once

#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Graphite {

	// The language server protocol's boundary conversions and reply parsers, kept apart from
	// LspClient because they are the whole of what it does that can be checked without a process.
	// The client is the transport around them: the process, its two threads, the handshake and
	// the correlation of replies with the questions they answer.

	// A path as the protocol spells it: forward slashes, a file:/// prefix, and percent-encoding
	// for everything outside the unreserved set. The encoding is not cosmetic — clangd compares
	// URIs as strings, so a path that reaches it under two different spellings is two different
	// files to it, and every answer about the second one comes back empty.
	std::string ToFileUri(const std::filesystem::path& path);
	std::filesystem::path FromFileUri(std::string_view uri);

	// The protocol counts columns in UTF-16 code units and this editor counts bytes. Both
	// conversions walk `line`'s UTF-8 lead bytes, counting one unit per code point below U+10000
	// and two at or above — a code point at or above U+10000 is four bytes here and a surrogate
	// pair there. A column past the end of the line clamps to its end rather than running off it.
	//
	// Neither is called by LspClient, deliberately. The caller converts, because the caller holds
	// the buffer the column belongs to: the client has only the file on disk, and for a document
	// with unsaved edits that is a different file, so converting there would answer confidently
	// about text the user is not looking at. There is still exactly one pair of these functions,
	// and they are the only way to obtain or spend a Utf16Column.
	Utf16Column ToUtf16Column(std::string_view line, std::size_t byteColumn);
	std::size_t FromUtf16Column(std::string_view line, Utf16Column column);

	// Both tolerate null, a wrong type and missing fields by yielding less, never by failing — a
	// server is allowed to answer nothing, and ADR 0004 forbids throwing across this boundary.
	std::vector<Location> ParseLocations(const nlohmann::json& result);

	// clangd's own extension, not part of the protocol: the reply is a bare URI string, or null
	// when the file has no counterpart. Anything else is read as null.
	std::optional<Location> ParseSwitchSourceHeader(const nlohmann::json& result);

	// Keeps only the symbol kinds this editor has somewhere to show — functions and types — and
	// flattens the tree, parents before children, so a caller never walks it again. Anything else
	// is skipped rather than stored as DeclarationKind::Other, so no lens appears over a variable.
	std::vector<Declaration> ParseSymbols(const nlohmann::json& result);

	// The server's own numbering, read from its initialize reply. Index N of the returned vector
	// is what this editor colours the server's token type N as, so a reply's raw integers can be
	// turned into something meaningful. Empty when the server offers no semantic tokens at all,
	// which is the signal not to ask for them.
	std::vector<SemanticKind> ParseSemanticLegend(const nlohmann::json& initializeResult);

	// Decodes the flat run of integers a semanticTokens reply carries - five per token, each
	// relative to the token before it - into absolute positions. A token whose type index the
	// legend does not cover is dropped: a server and a legend that disagree is not something to
	// colour by approximation.
	std::vector<SemanticToken> ParseSemanticTokens(const nlohmann::json& result,
												   const std::vector<SemanticKind>& legend);

	// One textDocument/publishDiagnostics notification's params. Tolerates null, a wrong type and
	// missing fields by yielding less, never by failing, exactly as the reply parsers above do:
	// a notification is not a request, so there is nobody to report a failure to.
	//
	// Severity is filtered here rather than by a reader. Information and Hint exist in the
	// protocol and are not drawn anywhere in this application; dropping them at the boundary is
	// what stops every downstream switch from needing a case that cannot happen.
	PublishedDiagnostics ParseDiagnostics(const nlohmann::json& params);

	// One textDocument/diagnostic answer: the items of a full report, read exactly as a
	// publication's items are. nullopt for an "unchanged" report, which this client never asks
	// for since it sends no previousResultId, and for anything that is not a report at all.
	std::optional<std::vector<Diagnostic>> ParseDiagnosticReport(const nlohmann::json& result);

	// Whether the initialize result offers diagnostic pulls (capabilities.diagnosticProvider). A
	// server that only publishes leaves it out: clangd does, Roslyn and csharp-ls do not.
	bool OffersDiagnosticPulls(const nlohmann::json& initializeResult);
}

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

	std::string ToFileUri(const std::filesystem::path& path);
	std::filesystem::path FromFileUri(std::string_view uri);

	// WARNING: LspClient must not call these two. It sees only the file on disk, which for a document
	// with unsaved edits is not the text the column belongs to; the caller holds the buffer and converts.
	Utf16Column ToUtf16Column(std::string_view line, std::size_t byteColumn);
	std::size_t FromUtf16Column(std::string_view line, Utf16Column column);

	std::vector<Location> ParseLocations(const nlohmann::json& result);

	std::optional<Location> ParseSwitchSourceHeader(const nlohmann::json& result);

	std::vector<Declaration> ParseSymbols(const nlohmann::json& result);

	std::vector<SemanticKind> ParseSemanticLegend(const nlohmann::json& initializeResult);

	std::vector<SemanticToken> ParseSemanticTokens(const nlohmann::json& result,
												   const std::vector<SemanticKind>& legend);

	PublishedDiagnostics ParseDiagnostics(const nlohmann::json& params);

	std::optional<std::vector<Diagnostic>> ParseDiagnosticReport(const nlohmann::json& result);

	bool OffersDiagnosticPulls(const nlohmann::json& initializeResult);
}

#pragma once

#include "Language/Api/LanguageService.h"
#include "UI/FileIcons.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	inline constexpr float kBreadcrumbHeight = 24.0f;
	inline constexpr float kBreadcrumbFontSize = 15.0f;
	inline constexpr float kBreadcrumbMarkFontSize = 15.0f;
	inline constexpr float kBreadcrumbLeftPadding = 12.0f;

	struct BreadcrumbSymbol
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Function;

		bool operator==(const BreadcrumbSymbol&) const = default;
	};

	struct BreadcrumbModel
	{
		std::vector<std::string> Folders;
		std::string FileName;
		FileKind Kind = FileKind::Other;
		std::vector<BreadcrumbSymbol> Symbols;

		bool operator==(const BreadcrumbModel&) const = default;
	};

	// What a tab's view puts on the right of the row: Width is in screen pixels, and Draw runs with the cursor at the
	// tools' left edge, inside the row.
	struct BreadcrumbTools
	{
		float Width = 0.0f;
		std::function<void()> Draw;
	};

	BreadcrumbModel BuildBreadcrumb(const std::filesystem::path& root, const std::filesystem::path& path,
									std::span<const Declaration> declarations, std::size_t caretLine);

	float BreadcrumbHeight();

	float BreadcrumbWidth(const BreadcrumbModel& model, std::size_t firstShownFolder);

	std::size_t FirstShownFolder(const BreadcrumbModel& model, float room);

	void DrawBreadcrumb(const BreadcrumbModel& model, const BreadcrumbTools& tools = {});
}

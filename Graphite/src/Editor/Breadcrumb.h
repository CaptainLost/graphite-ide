#pragma once

#include "Language/Api/LanguageService.h"
#include "UI/FileIcons.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	inline constexpr float kBreadcrumbHeight = 26.0f;
	inline constexpr float kBreadcrumbFontSize = 13.0f;
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

	BreadcrumbModel BuildBreadcrumb(const std::filesystem::path& root, const std::filesystem::path& path,
									std::span<const Declaration> declarations, std::size_t caretLine);

	float BreadcrumbHeight();

	float BreadcrumbWidth(const BreadcrumbModel& model, std::size_t firstShownFolder);

	std::size_t FirstShownFolder(const BreadcrumbModel& model, float room);

	void DrawBreadcrumb(const BreadcrumbModel& model);
}

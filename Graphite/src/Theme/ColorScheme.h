#pragma once

#include "Core/Base/Color.h"
#include "Language/Api/TokenKind.h"
#include "Theme/BuiltInScheme.h"
#include "Theme/ChromeColor.h"
#include "Theme/EditorColor.h"
#include "Theme/GitColor.h"
#include "Theme/LayoutMetric.h"
#include "Theme/SessionColor.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace Graphite {

	struct ColorScheme
	{
		BuiltInScheme Base = BuiltInScheme::Dark;

		std::array<std::optional<Core::Color>, static_cast<std::size_t>(TokenKind::Count)> Syntax;
		std::array<std::optional<Core::Color>, static_cast<std::size_t>(EditorColor::Count)>
			Editor;
		std::array<std::optional<Core::Color>, static_cast<std::size_t>(ChromeColor::Count)>
			Chrome;
		std::array<std::optional<Core::Color>, static_cast<std::size_t>(GitColor::Count)> Git;
		std::array<std::optional<Core::Color>, static_cast<std::size_t>(SessionColor::Count)>
			Session;
		std::array<std::optional<float>, kLayoutMetricCount> Layout;
	};

	struct ResolvedScheme
	{
		std::array<Core::Color, static_cast<std::size_t>(TokenKind::Count)> Syntax;
		std::array<Core::Color, static_cast<std::size_t>(EditorColor::Count)> Editor;
		std::array<Core::Color, static_cast<std::size_t>(ChromeColor::Count)> Chrome;
		std::array<Core::Color, static_cast<std::size_t>(GitColor::Count)> Git;
		std::array<Core::Color, static_cast<std::size_t>(SessionColor::Count)> Session;
		std::array<float, kLayoutMetricCount> Layout = DefaultLayout();
	};

	struct ColorNames
	{
		std::string_view Key;
		std::string_view Label;
	};

	ColorNames NamesOf(TokenKind kind);
	ColorNames NamesOf(EditorColor color);
	ColorNames NamesOf(ChromeColor color);
	ColorNames NamesOf(GitColor color);
	ColorNames NamesOf(SessionColor color);
	ColorNames NamesOf(LayoutMetric metric);

	std::string_view LabelOf(TokenKind kind);
	std::string_view LabelOf(EditorColor color);
	std::string_view LabelOf(ChromeColor color);
	std::string_view LabelOf(GitColor color);
	std::string_view LabelOf(SessionColor color);
	std::string_view LabelOf(LayoutMetric metric);

	std::string_view NameOf(TokenKind kind);
	std::string_view NameOf(EditorColor color);
	std::string_view NameOf(ChromeColor color);
	std::string_view NameOf(GitColor color);
	std::string_view NameOf(SessionColor color);
	std::string_view NameOf(LayoutMetric metric);

	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, TokenKind slot);
	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, EditorColor slot);
	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, ChromeColor slot);
	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, GitColor slot);
	const Core::Color& ColourFromScheme(const ResolvedScheme& scheme, SessionColor slot);

	ResolvedScheme ResolveSchemeOverBase(const ColorScheme& scheme, const ResolvedScheme& base);
}

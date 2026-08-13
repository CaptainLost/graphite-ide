#pragma once

#include "Core/Base/Color.h"
#include "Lang/Api/TokenKind.h"
#include "Theme/BuiltInScheme.h"
#include "Theme/EditorColor.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace Graphite {

	// What a scheme file holds: the base it inherits from, and only the colours it changes.
	// Sparse on purpose — a scheme that overrides three colours keeps working when a later
	// stage adds a fourth, because the new one comes from the base.
	struct ColorScheme
	{
		BuiltInScheme Base = BuiltInScheme::Dark;

		std::array<std::optional<Core::Color>, static_cast<std::size_t>(TokenKind::Count)> Syntax;
		std::array<std::optional<Core::Color>, static_cast<std::size_t>(EditorColor::Count)>
			Editor;
	};

	// Every colour, with nothing left to decide. This is what the palettes are built from.
	struct ResolvedScheme
	{
		std::array<Core::Color, static_cast<std::size_t>(TokenKind::Count)> Syntax;
		std::array<Core::Color, static_cast<std::size_t>(EditorColor::Count)> Editor;
	};

	// A colour's two spellings, produced together so they cannot drift: Key is what the scheme
	// file stores, Label is what the settings row shows.
	struct ColorNames
	{
		std::string_view Key;
		std::string_view Label;
	};

	ColorNames NamesOf(TokenKind kind);
	ColorNames NamesOf(EditorColor color);

	std::string_view LabelOf(TokenKind kind);
	std::string_view LabelOf(EditorColor color);

	// The key text a colour is stored under, without its "syntax." or "editor." prefix. Shared
	// with sub-project C, which labels its rows from the same names rather than keeping a
	// second list that could drift.
	std::string_view NameOf(TokenKind kind);
	std::string_view NameOf(EditorColor color);

	ResolvedScheme Resolve(const ColorScheme& scheme, const ResolvedScheme& base);
}

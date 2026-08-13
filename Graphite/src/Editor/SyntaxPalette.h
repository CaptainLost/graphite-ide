#pragma once

#include "Lang/Api/TokenKind.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <tuple>

namespace Graphite {

	// One colour per token kind, read once a frame. Only Text comes from the theme; the rest are
	// IM_COL32 literals fixed until an editable colour scheme exists — a later stage's job.
	struct SyntaxPalette
	{
		// Sized from TokenKind::Count rather than a literal, so a kind added to TokenKind grows
		// this array instead of leaving Of reading past its end.
		std::array<ImU32, static_cast<std::size_t>(TokenKind::Count)> Colours{};

		ImU32 Of(TokenKind kind) const
		{
			return Colours[static_cast<std::size_t>(kind)];
		}
	};

	static_assert(std::tuple_size_v<decltype(SyntaxPalette::Colours)> ==
				  static_cast<std::size_t>(TokenKind::Count));

	SyntaxPalette ReadSyntaxPalette(ImU32 text);
}

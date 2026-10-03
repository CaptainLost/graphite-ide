#pragma once

#include "Theme/EditorPalette.h"

#include "Theme/GitPalette.h"
#include "Language/Api/TokenKind.h"
#include "Theme/ColorScheme.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <tuple>

namespace Graphite {

	// One colour per token kind, read once a frame.
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

	// The ImGui boundary for both palettes: the one place in this sub-project a Core::Color
	// turns into an ImU32, via IM_COL32. Theme/ stays free of imgui.h so it can build and be
	// tested without a window.
	SyntaxPalette ReadSyntaxPalette(const ResolvedScheme& scheme);

	EditorPalette ReadEditorPalette(const ResolvedScheme& scheme);
}

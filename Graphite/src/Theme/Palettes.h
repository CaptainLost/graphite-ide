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

	struct SyntaxPalette
	{
		std::array<ImU32, static_cast<std::size_t>(TokenKind::Count)> Colours{};

		ImU32 Of(TokenKind kind) const
		{
			return Colours[static_cast<std::size_t>(kind)];
		}
	};

	static_assert(std::tuple_size_v<decltype(SyntaxPalette::Colours)> ==
				  static_cast<std::size_t>(TokenKind::Count));

	SyntaxPalette ReadSyntaxPalette(const ResolvedScheme& scheme);

	EditorPalette ReadEditorPalette(const ResolvedScheme& scheme);

	inline constexpr ImVec4 kErrorTextColour{0.94f, 0.42f, 0.42f, 1.0f};

	ImVec4 DangerColourOf(const ResolvedScheme& scheme);
}

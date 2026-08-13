#pragma once

#include "Lang/Api/TokenKind.h"

#include <imgui.h>

#include <array>
#include <cstddef>

namespace Graphite {

	// One colour per token kind, read once a frame. Derived from the theme rather than written
	// as literals so the colours still make sense if the theme changes; a proper editable colour
	// scheme is a later stage's job.
	struct SyntaxPalette
	{
		std::array<ImU32, 10> Colours{};

		ImU32 Of(TokenKind kind) const
		{
			return Colours[static_cast<std::size_t>(kind)];
		}
	};

	SyntaxPalette ReadSyntaxPalette(ImU32 text);
}

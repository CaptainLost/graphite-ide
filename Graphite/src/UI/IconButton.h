#pragma once

namespace Graphite {

	float IconButtonSize();

	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip);

	constexpr float kIconSeparatorWidth = 1.0f;

	void DrawIconSeparator();
}

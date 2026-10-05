#include "corepch.h"

#include "Core/ImGui/TextMetrics.h"

#include <imgui.h>

namespace Core {

	float MonospaceCellWidth()
	{
		// The baked size is whatever ImGui rounded the pushed size to, which is not necessarily
		// the size in effect (global and DPI scale factors are applied to GetFontSize() but the
		// bake is picked from the rounded result), so the advance is scaled from the bake to the
		// current size exactly as ImFont::RenderText does.
		//
		// Non-const because GetCharAdvance builds the glyph on demand if this size has not seen
		// it yet - this ImGui bakes lazily, so the first frame at a new zoom level is the one
		// that loads it.
		ImFontBaked* const baked = ImGui::GetFontBaked();

		// Every glyph in a monospaced face advances the same, so one glyph's advance is the
		// width of every cell.
		return baked->GetCharAdvance('0') * (ImGui::GetFontSize() / baked->Size);
	}

	// An empty view may carry a null pointer, which CalcTextSize would read as "up to the terminator".
	float TextWidth(std::string_view text)
	{
		if (text.empty())
		{
			return 0.0f;
		}

		return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
	}
}

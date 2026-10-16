#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace Core {

	enum class IconButtonFit
	{
		// A square a little taller than a line, centred in a row one frame high: a toolbar or a bar.
		Frame,

		// A square one line of text high, for a row of a list, which must not grow taller for it.
		Line
	};

	struct IconButtonOptions
	{
		IconButtonFit Fit = IconButtonFit::Frame;

		ImGuiButtonFlags Flags = ImGuiButtonFlags_None;
	};

	float IconButtonSize(IconButtonFit fit = IconButtonFit::Frame);

	// No fill at rest, a rounded fill under the pointer, the active fill while held; the tooltip waits for the pointer
	// to rest. Every button that is only a glyph looks like this, wherever it is. An engaged one, a toggle that is on,
	// is filled and drawn in the accent.
	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip,
						const IconButtonOptions& options = {});

	// For a button whose press is read elsewhere, as when its row must be drawn over it: only the look, in `rect`.
	void DrawIconButtonFace(const ImRect& rect, const char* glyph, bool hovered, bool held);

	constexpr float kIconSeparatorWidth = 1.0f;

	void DrawIconSeparator();
}

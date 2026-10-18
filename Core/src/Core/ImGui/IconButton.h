#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace Core {

	enum class IconButtonFit
	{
		Frame,
		Line
	};

	struct IconButtonOptions
	{
		IconButtonFit Fit = IconButtonFit::Frame;

		ImGuiButtonFlags Flags = ImGuiButtonFlags_None;
	};

	float IconButtonSize(IconButtonFit fit = IconButtonFit::Frame);

	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip,
						const IconButtonOptions& options = {});

	void DrawIconButtonFace(const ImRect& rect, const char* glyph, bool hovered, bool held);

	void DrawCentredGlyph(ImDrawList* drawList, const ImRect& rect, const char* glyph, ImU32 colour);

	constexpr float kIconSeparatorWidth = 1.0f;

	void DrawIconSeparator();
}

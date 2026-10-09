#pragma once

#include "Core/Base/Color.h"
#include "Core/ImGui/Toasts.h"

#include <imgui.h>

namespace Core {

	float ToastCardWidth();
	float ToastMargin();
	float ToastPaddingX();
	float ToastPaddingY();
	float ToastIconDiameter();

	const Color& ToastAccentOf(const ToastPalette& palette, ToastKind kind);
	Color ToastMessageColour(const ToastPalette& palette);

	// Takes ImGui's style alpha, so a toast fading out fades its icon with it.
	void DrawToastIcon(ImDrawList& draw, ToastKind kind, ImVec2 centre, const ToastPalette& palette);
}

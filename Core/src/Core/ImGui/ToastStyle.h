#pragma once

#include "Core/Base/Color.h"
#include "Core/ImGui/ToastText.h"
#include "Core/ImGui/Toasts.h"

#include <imgui.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace Core {

	float ToastCardWidth();
	float ToastMargin();
	float ToastPaddingX();
	float ToastPaddingY();
	float ToastIconDiameter();

	const Color& ToastAccentOf(const ToastPalette& palette, ToastKind kind);
	Color ToastMessageColour(const ToastPalette& palette);

	void DrawToastIcon(ImDrawList& drawList, ToastKind kind, ImVec2 centre, const ToastPalette& palette);

	enum class NotificationTitleFit : std::uint8_t
	{
		Wrap,
		Ellipsis
	};

	float NotificationIconGap();
	float NotificationTitleGap();

	ImVec2 NotificationIconCentre(float textLeft, float firstLineTop);

	float NotificationTitleHeight(std::string_view title, std::string_view count, float widthWithCount,
								  NotificationTitleFit fit);

	void DrawNotificationTitle(ImDrawList& drawList, ImVec2 topLeft, float widthWithCount, std::string_view title,
							   std::string_view count, NotificationTitleFit fit, const ToastPalette& palette);

	void DrawNotificationMessage(ImDrawList& drawList, ImVec2 topLeft, std::span<const ToastTextLine> lines,
								 const ToastPalette& palette);
}

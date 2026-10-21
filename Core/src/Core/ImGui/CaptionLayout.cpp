#include "corepch.h"

#include "Core/ImGui/CaptionLayout.h"

namespace Core {

	namespace {
		constexpr float kBarTop = 0.0f;

		CaptionRect RectAcrossBar(float left, float width, float barHeight)
		{
			return CaptionRect{.X = left, .Y = kBarTop, .Width = width, .Height = barHeight};
		}
	}

	CaptionLayout LayOutCaption(float barWidth, float barHeight, float buttonWidth, float contentLeft,
								const CaptionTitleWidths& widths, const CaptionTitleSpacing& spacing)
	{
		CaptionLayout layout;

		// WARNING: measured from the right edge, so a bar narrower than its own buttons runs off the left rather than pushing close off screen, and minimize is the first one to go.
		layout.Close = RectAcrossBar(barWidth - buttonWidth, buttonWidth, barHeight);
		layout.Maximize = RectAcrossBar(layout.Close.X - buttonWidth, buttonWidth, barHeight);
		layout.Minimize = RectAcrossBar(layout.Maximize.X - buttonWidth, buttonWidth, barHeight);

		if (widths.Folder <= 0.0f)
		{
			return layout;
		}

		const float limit = layout.Minimize.X - spacing.ButtonsGap;

		const float dividerLeft = contentLeft + spacing.Margin;
		const float folderLeft = dividerLeft + spacing.DividerWidth + spacing.Margin;

		layout.Divider = RectAcrossBar(dividerLeft, spacing.DividerWidth, barHeight);
		layout.Folder = RectAcrossBar(folderLeft, widths.Folder, barHeight);
		layout.FolderVisible = folderLeft + widths.Folder <= limit;
		layout.DividerVisible = layout.FolderVisible;

		return layout;
	}
}

#pragma once

namespace Core {

	struct CaptionRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
	};

	// A width of zero is a label that is not there.
	struct CaptionTitleWidths
	{
		float Folder = 0.0f;
		float Branch = 0.0f;
	};

	struct CaptionTitleSpacing
	{
		float Margin = 0.0f;
		float DividerWidth = 0.0f;
		float GroupGap = 0.0f;
		float ButtonsGap = 0.0f;
	};

	struct CaptionLayout
	{
		CaptionRect Minimize;
		CaptionRect Maximize;
		CaptionRect Close;

		CaptionRect Divider;
		CaptionRect Folder;
		CaptionRect Branch;

		bool DividerVisible = false;
		bool FolderVisible = false;
		bool BranchVisible = false;
	};

	CaptionLayout LayOutCaption(float barWidth, float barHeight, float buttonWidth, float contentLeft,
								const CaptionTitleWidths& widths, const CaptionTitleSpacing& spacing);
}

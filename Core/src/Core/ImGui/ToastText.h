#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	enum class ToastTextStyle
	{
		Plain,

		// Text in single quotes - a branch, a ref - drawn in the bold face, quotes kept.
		Strong,

		// An absolute path, or one relative to the current or a hidden folder.
		Path
	};

	struct ToastTextRun
	{
		std::string Text;
		ToastTextStyle Style = ToastTextStyle::Plain;

		// From the line's left edge, and the run's own width, as the measure gave them.
		float X = 0.0f;
		float Width = 0.0f;
	};

	struct ToastTextLine
	{
		std::vector<ToastTextRun> Runs;
	};

	// The width of a piece of text, in the bold face when `bold` is set. Passed in so the
	// layout can be tested with a fixed advance per character, and measured in production
	// with the font the text is drawn in.
	using ToastTextMeasure = std::function<float(std::string_view text, bool bold)>;

	// Lays a toast's message out in lines no wider than `width`, instead of ImGui's word wrap,
	// which breaks a path at any '-' or '.' and leaves "D:/Repos/IDE/." on one line and
	// "claude/..." on the next. Plain words wrap at spaces. A path starts a line of its own and
	// breaks only after a separator; a quoted span never breaks inside. A word, a quoted span
	// or a path segment wider than a whole line breaks wherever it has to, as a last resort.
	// A newline in the message starts a new line.
	std::vector<ToastTextLine> LayoutToastText(std::string_view message, float width, const ToastTextMeasure& measure);
}

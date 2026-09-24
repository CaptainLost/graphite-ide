#pragma once

#include "Sessions/ToolFacts.h"

#include <imgui.h>

#include <span>
#include <string>

namespace Graphite {

	// The pieces the session views draw their one-line rows from, shared so a request listed in
	// the Requests tab and under a usage line in the conversation reads the same.

	inline constexpr const char* kEllipsis = "\xe2\x80\xa6";
	inline constexpr const char* kDot = " \xc2\xb7 ";

	void ColouredText(ImU32 colour, const std::string& text);

	// A thin vertical bar at the left of the row just begun, in the category's colour.
	void CategoryBar(ImU32 colour, float height);

	// Text that is cut with an ellipsis to fit `width`, never wrapped. Measured once: the
	// font says where the text stops fitting, which is always between two characters, so
	// a multi-byte character is never split.
	std::string Fitted(const std::string& text, float width);

	const char* IconOf(ToolCategory category);

	// Draws `text` at `at` and moves `at` past it. The one-line rows are drawn through the
	// draw list over a Selectable or a Dummy, so each piece can take its own colour and the
	// row stays exactly one item tall.
	void DrawPiece(ImVec2& at, ImU32 colour, const std::string& text);

	// A small button drawn pressed in while `selected`: one option of a choice. Styled as a
	// button rather than a tab, so a row of them under the session's own tabs does not read as
	// another row of tabs. Returns whether it was clicked while not already selected.
	bool SegmentButton(const char* label, bool selected);

	// `labels` as a row of SegmentButtons joined edge to edge, so the row reads as one control;
	// `selected` is the index of the chosen one, and moves to the one clicked. Returns whether
	// it moved.
	bool SegmentedChoice(std::span<const char* const> labels, int& selected);
}

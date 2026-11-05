#pragma once

namespace Graphite {

	// The widths, in px, of what the history header draws after its icon, each at the size it would take with room to spare.
	struct HeaderWidths
	{
		float Row = 0.0f;       // the header's whole width
		float Taken = 0.0f;     // the icon and the gap after it, from the row's left edge
		float VsButton = 0.0f;  // the vs button; 0 when the header draws none
		float Switch = 0.0f;
		float Operation = 0.0f; // the operation in progress with the gap before it; 0 for none
		float Prefix = 0.0f;    // a remote branch's dim remote
		float Title = 0.0f;     // the bold name
		float Fields = 0.0f;    // both filter fields at their full widths with the gap between them
		float Spacing = 0.0f;   // the gap between two items on the line
	};

	struct HeaderShares
	{
		float VsButton = 0.0f;
		float NameRoom = 0.0f; // for the prefix and the name; none of the shares is ever below zero
	};

	// As in the mockup, the vs button and the switch keep their widths (the button at most this share of the row), and the
	// name and the filter fields share the rest in proportion to the widths they would take.
	inline constexpr float kVsButtonShare = 0.4f;

	HeaderShares ShareHeader(const HeaderWidths& widths);

	struct FilterFieldWidths
	{
		float Words = 0.0f;
		float Path = 0.0f;
	};

	// Short of room, the path field gives up its share as the message field does; neither is below zero.
	FilterFieldWidths SplitFilterFields(float room, float fullPathWidth, float fieldsBasis, float spacing);
}

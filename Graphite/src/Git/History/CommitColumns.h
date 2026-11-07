#pragma once

namespace Graphite {

	struct CommitColumnsInput
	{
		float Available = 0.0f;
		float Graph = 0.0f;

		// The pixels of the interface font; the thresholds are the mockup's, in its 13 pixel em.
		float FontSize = 0.0f;
	};

	struct CommitColumns
	{
		float Graph = 0.0f;

		// WARNING: Read from the width of the table alone, never the data, so the hash comes and goes only when the reader resizes.
		bool ShowHash = true;
	};

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input);

	// The author's cell shows the name, cut with an ellipsis, while the column has room for the avatar, the gap after it
	// and about three characters; below that the avatar stands alone, with the name in its tooltip. Read from the
	// column's width as the reader set it, so the same column shows the same choice on every row.
	bool ShowsAuthorName(float columnWidth, float avatarAndGap, float characterWidth);
}

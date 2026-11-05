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

		// WARNING: Both read the width of the table alone, never the data, so the columns change only when the reader resizes.
		bool ShowAuthorName = true;
		bool ShowHash = true;
	};

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input);
}

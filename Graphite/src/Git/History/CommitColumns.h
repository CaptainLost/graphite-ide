#pragma once

namespace Graphite {

	struct CommitColumnsInput
	{
		float Available = 0.0f;

		float Graph = 0.0f;
		float Chevron = 0.0f;
		float Hash = 0.0f;
		float Author = 0.0f;
		float Date = 0.0f;

		float CellPadding = 0.0f;

		float MinMessage = 0.0f;
	};

	struct CommitColumns
	{
		float Graph = 0.0f;
		bool ShowAuthor = true;
		bool ShowDate = true;

		// WARNING: Message is negative when even the always-shown columns do not fit.
		float Message = 0.0f;
	};

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input);
}

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	enum class DiffLineKind
	{
		Context,
		Added,
		Removed
	};

	struct DiffLine
	{
		DiffLineKind Kind = DiffLineKind::Context;

		// Zero where the line does not exist on that side: an added line has no old
		// number, a removed line has no new one.
		int OldLine = 0;
		int NewLine = 0;

		// Includes the line terminator when the file has one, so a missing final newline
		// is visible in the text rather than only in a flag.
		std::string Text;
	};

	struct Hunk
	{
		int OldStart = 0;
		int OldCount = 0;
		int NewStart = 0;
		int NewCount = 0;

		// The "@@ -a,b +c,d @@" line, kept as libgit2 formatted it so the unified view
		// does not have to rebuild it.
		std::string Header;

		std::vector<DiffLine> Lines;
	};

	enum class FileChange
	{
		Unmodified,
		Added,
		Modified,
		Deleted,
		Renamed,
		Copied,
		TypeChanged
	};

	struct FileDiff
	{
		// Both are relative to the working directory. For anything but a rename or a copy
		// they hold the same path; for an add, OldPath is empty, and for a delete NewPath is.
		std::filesystem::path OldPath;
		std::filesystem::path NewPath;

		FileChange Change = FileChange::Unmodified;

		// A binary delta has no hunks. The view must check this before drawing lines.
		bool IsBinary = false;

		std::vector<Hunk> Hunks;
	};

	struct Diff
	{
		std::vector<FileDiff> Files;
	};
}

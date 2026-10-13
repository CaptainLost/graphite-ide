#pragma once

#include "Core/Git/Oid.h"

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

		// Zero on the side where the line does not exist: an added line has no old number,
		// a removed line no new one.
		int OldLine = 0;
		int NewLine = 0;

		// Includes the line terminator when the file has one, so a missing final newline
		// shows in the text.
		std::string Text;
	};

	struct Hunk
	{
		int OldStart = 0;
		int OldCount = 0;
		int NewStart = 0;
		int NewCount = 0;

		// The "@@ -a,b +c,d @@" line exactly as libgit2 formatted it.
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
		// Relative to the working directory, and the same path unless the file was renamed
		// or copied. An add leaves OldPath empty, a delete leaves NewPath empty.
		std::filesystem::path OldPath;
		std::filesystem::path NewPath;

		FileChange Change = FileChange::Unmodified;

		// Zero for a side that does not exist. A working-tree side has the id git gives its current
		// content, so the pair names this exact change of the file whatever the target.
		Oid OldId;
		Oid NewId;

		// A binary delta has no hunks, so an empty Hunks cannot tell it from a hunkless change; check
		// this flag before drawing lines.
		bool IsBinary = false;

		std::vector<Hunk> Hunks;
	};

	struct Diff
	{
		std::vector<FileDiff> Files;
	};
}

#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Status.h"
#include "Git/GitModel.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

inline Core::Oid OidWithFirstByte(std::uint8_t firstByte)
{
	Core::Oid id;
	id.Bytes[0] = firstByte;
	return id;
}

inline Graphite::JobSubmitter RunJobsInline()
{
	return [](std::function<void()> job) { job(); };
}

inline Core::WorkingTreeStatus WorkingTreeStatusOf(std::vector<Core::FileStatus> entries)
{
	Core::WorkingTreeStatus status;
	status.Entries = std::move(entries);
	return status;
}

inline Core::DiffLine MakeDiffLine(Core::DiffLineKind kind, int oldLine, int newLine, std::string text)
{
	Core::DiffLine line;
	line.Kind = kind;
	line.OldLine = oldLine;
	line.NewLine = newLine;
	line.Text = std::move(text);
	return line;
}

inline Core::FileDiff MakeFileDiff(std::vector<Core::DiffLine> lines)
{
	Core::Hunk hunk;
	hunk.OldStart = 1;
	hunk.NewStart = 1;
	hunk.Header = "@@ -1 +1 @@\n";
	hunk.Lines = std::move(lines);

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));
	return file;
}

inline std::chrono::sys_seconds SystemSecondsAt(long long seconds)
{
	return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
}

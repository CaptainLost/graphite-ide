#include "Git/DiffRows.h"

namespace Graphite {

	namespace {

		DiffRowKind KindOf(Core::DiffLineKind kind)
		{
			switch (kind)
			{
				case Core::DiffLineKind::Added: return DiffRowKind::Added;
				case Core::DiffLineKind::Removed: return DiffRowKind::Removed;
				case Core::DiffLineKind::Context: break;
			}

			return DiffRowKind::Context;
		}

		DiffRow RowFor(const Core::DiffLine& line, std::size_t hunk, std::size_t index)
		{
			DiffRow row;
			row.Kind = KindOf(line.Kind);
			row.OldLine = line.OldLine;
			row.NewLine = line.NewLine;
			row.Hunk = hunk;
			row.Line = index;
			return row;
		}

		DiffRow HeaderFor(std::size_t hunk)
		{
			DiffRow row;
			row.Kind = DiffRowKind::HunkHeader;
			row.Hunk = hunk;
			return row;
		}
	}

	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file)
	{
		std::vector<DiffRow> rows;

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			rows.push_back(HeaderFor(hunk));

			const std::vector<Core::DiffLine>& lines = file.Hunks[hunk].Lines;

			for (std::size_t index = 0; index < lines.size(); ++index)
			{
				rows.push_back(RowFor(lines[index], hunk, index));
			}
		}

		return rows;
	}

	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file)
	{
		std::vector<DiffPair> pairs;

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			pairs.push_back(DiffPair{HeaderFor(hunk), HeaderFor(hunk)});

			const std::vector<Core::DiffLine>& lines = file.Hunks[hunk].Lines;

			std::size_t index = 0;

			while (index < lines.size())
			{
				if (lines[index].Kind == Core::DiffLineKind::Context)
				{
					const DiffRow row = RowFor(lines[index], hunk, index);
					pairs.push_back(DiffPair{row, row});
					++index;
					continue;
				}

				// A run of removals followed by a run of insertions is git's way of
				// spelling "these lines became those lines". Zipping the two runs is what
				// turns that back into one row per changed line.
				const std::size_t removedFirst = index;

				while (index < lines.size() && lines[index].Kind == Core::DiffLineKind::Removed)
				{
					++index;
				}

				const std::size_t removedCount = index - removedFirst;
				const std::size_t addedFirst = index;

				while (index < lines.size() && lines[index].Kind == Core::DiffLineKind::Added)
				{
					++index;
				}

				const std::size_t addedCount = index - addedFirst;
				const std::size_t rows = removedCount > addedCount ? removedCount : addedCount;

				for (std::size_t offset = 0; offset < rows; ++offset)
				{
					DiffPair pair;

					if (offset < removedCount)
					{
						pair.Left = RowFor(lines[removedFirst + offset], hunk,
										   removedFirst + offset);
					}

					if (offset < addedCount)
					{
						pair.Right = RowFor(lines[addedFirst + offset], hunk,
											addedFirst + offset);
					}

					pairs.push_back(pair);
				}
			}
		}

		return pairs;
	}

	const Core::DiffLine* LineOf(const Core::FileDiff& file, const DiffRow& row)
	{
		if (row.Kind == DiffRowKind::HunkHeader || row.Hunk >= file.Hunks.size())
		{
			return nullptr;
		}

		const std::vector<Core::DiffLine>& lines = file.Hunks[row.Hunk].Lines;

		if (row.Line >= lines.size())
		{
			return nullptr;
		}

		return &lines[row.Line];
	}
}

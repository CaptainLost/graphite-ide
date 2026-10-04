#include "Git/Diff/DiffSelection.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace Graphite {

	namespace {

		// Every offset a caret can sit at: the start, the end and each UTF-8 character boundary
		// between them.
		std::vector<std::size_t> Boundaries(std::string_view text)
		{
			std::vector<std::size_t> boundaries;
			boundaries.reserve(text.size() + 1);

			// Offset 0 is always one, even when the text opens on a stray continuation byte.
			boundaries.push_back(0);

			for (std::size_t i = 1; i < text.size(); ++i)
			{
				if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
				{
					boundaries.push_back(i);
				}
			}

			if (!text.empty())
			{
				boundaries.push_back(text.size());
			}

			return boundaries;
		}

		// Index of the first boundary for which `reached` holds; the count when none does.
		template <typename Predicate>
		std::size_t FirstBoundary(const std::vector<std::size_t>& boundaries, Predicate reached)
		{
			std::size_t low = 0;
			std::size_t high = boundaries.size();

			while (low < high)
			{
				const std::size_t middle = low + (high - low) / 2;

				if (reached(boundaries[middle]))
				{
					high = middle;
				}
				else
				{
					low = middle + 1;
				}
			}

			return low;
		}

		bool IsSelectable(DiffRowKind kind)
		{
			return kind == DiffRowKind::Context || kind == DiffRowKind::Added ||
				   kind == DiffRowKind::Removed || kind == DiffRowKind::Unchanged;
		}

		std::optional<std::int64_t> FileLineOf(const DiffRow& row, DiffSide side)
		{
			if (!IsSelectable(row.Kind))
			{
				return std::nullopt;
			}

			const int line = side == DiffSide::Old ? row.OldLine : row.NewLine;

			if (line <= 0)
			{
				return std::nullopt;
			}

			return line;
		}
	}

	std::optional<std::int64_t> UnifiedLineOf(std::span<const DiffRow> rows, std::size_t index)
	{
		if (index >= rows.size() || !IsSelectable(rows[index].Kind))
		{
			return std::nullopt;
		}

		return static_cast<std::int64_t>(index);
	}

	std::optional<std::int64_t> SideLineOf(const DiffPair& pair, DiffSide side)
	{
		const std::optional<DiffRow>& row = side == DiffSide::Old ? pair.Left : pair.Right;

		if (!row.has_value())
		{
			return std::nullopt;
		}

		return FileLineOf(*row, side);
	}

	std::string SelectedUnifiedText(const Core::TextSelection& selection, std::span<const DiffRow> rows,
									const Core::FileDiff& file,
									std::span<const std::string_view> oldLines,
									std::span<const std::string_view> newLines,
									const FileTokens& tokens)
	{
		if (selection.IsEmpty() || rows.empty())
		{
			return {};
		}

		const std::int64_t first = std::max<std::int64_t>(selection.First().Line, 0);
		const std::int64_t last =
			std::min<std::int64_t>(selection.Last().Line, static_cast<std::int64_t>(rows.size()) - 1);

		std::vector<Core::SelectableLine> lines;

		for (std::int64_t index = first; index <= last; ++index)
		{
			const std::size_t row = static_cast<std::size_t>(index);

			if (!UnifiedLineOf(rows, row).has_value())
			{
				continue;
			}

			const ResolvedLine resolved =
				ResolveRow(file, rows[row], DiffSide::New, oldLines, newLines, tokens);

			if (resolved.Text.has_value())
			{
				lines.push_back(Core::SelectableLine{index, *resolved.Text});
			}
		}

		return Core::JoinSelected(selection, lines);
	}

	std::string SelectedSideText(const Core::TextSelection& selection, DiffSide side,
								 std::span<const DiffPair> pairs, const Core::FileDiff& file,
								 std::span<const std::string_view> oldLines,
								 std::span<const std::string_view> newLines,
								 const FileTokens& tokens)
	{
		if (selection.IsEmpty())
		{
			return {};
		}

		const std::int64_t first = selection.First().Line;
		const std::int64_t last = selection.Last().Line;

		std::vector<Core::SelectableLine> lines;

		for (const DiffPair& pair : pairs)
		{
			const std::optional<std::int64_t> number = SideLineOf(pair, side);

			if (!number.has_value() || *number < first)
			{
				continue;
			}

			// Line numbers on one side only grow down the file, so nothing later is selected.
			if (*number > last)
			{
				break;
			}

			const DiffRow& row = side == DiffSide::Old ? *pair.Left : *pair.Right;
			const ResolvedLine resolved = ResolveRow(file, row, side, oldLines, newLines, tokens);

			if (resolved.Text.has_value())
			{
				lines.push_back(Core::SelectableLine{*number, *resolved.Text});
			}
		}

		return Core::JoinSelected(selection, lines);
	}

	namespace {

		void Combine(std::size_t& seed, std::size_t value)
		{
			seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
		}
	}

	std::size_t DiffContentHash(const Core::FileDiff& file)
	{
		std::size_t seed = file.Hunks.size();

		for (const Core::Hunk& hunk : file.Hunks)
		{
			Combine(seed, static_cast<std::size_t>(hunk.OldStart));
			Combine(seed, static_cast<std::size_t>(hunk.OldCount));
			Combine(seed, static_cast<std::size_t>(hunk.NewStart));
			Combine(seed, static_cast<std::size_t>(hunk.NewCount));
			Combine(seed, hunk.Lines.size());

			for (const Core::DiffLine& line : hunk.Lines)
			{
				Combine(seed, static_cast<std::size_t>(line.Kind));
				Combine(seed, static_cast<std::size_t>(line.OldLine));
				Combine(seed, static_cast<std::size_t>(line.NewLine));
				Combine(seed, std::hash<std::string_view>{}(line.Text));
			}
		}

		return seed;
	}

	std::size_t ByteAtX(std::string_view text, float x,
						const std::function<float(std::string_view prefix)>& widthOf)
	{
		if (text.empty() || x <= 0.0f)
		{
			return 0;
		}

		const std::vector<std::size_t> boundaries = Boundaries(text);
		const std::size_t next = FirstBoundary(boundaries, [&](std::size_t offset) {
			return widthOf(text.substr(0, offset)) >= x;
		});

		if (next >= boundaries.size())
		{
			return text.size();
		}

		if (next == 0)
		{
			return 0;
		}

		const float before = widthOf(text.substr(0, boundaries[next - 1]));
		const float after = widthOf(text.substr(0, boundaries[next]));

		return x - before < after - x ? boundaries[next - 1] : boundaries[next];
	}

	std::size_t ByteUnderX(std::string_view text, float x,
						   const std::function<float(std::string_view prefix)>& widthOf)
	{
		if (text.empty() || x < 0.0f)
		{
			return 0;
		}

		const std::vector<std::size_t> boundaries = Boundaries(text);

		// The first boundary strictly right of x ends the character x is inside.
		const std::size_t end = FirstBoundary(boundaries, [&](std::size_t offset) {
			return offset > 0 && widthOf(text.substr(0, offset)) > x;
		});

		if (end >= boundaries.size())
		{
			return text.size();
		}

		return end == 0 ? 0 : boundaries[end - 1];
	}
}

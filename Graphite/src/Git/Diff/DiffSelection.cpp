#include "Git/Diff/DiffSelection.h"

#include "Core/Text/Encoding.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::uint64_t kGoldenRatioSeed = 0x9e3779b97f4a7c15ull;
		constexpr std::size_t kSeedLeftShift = 6;
		constexpr std::size_t kSeedRightShift = 2;

		std::size_t FirstBoundary(const std::vector<std::size_t>& boundaries,
								  const std::function<bool(std::size_t offset)>& reached)
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

			const int fileLine = side == DiffSide::Old ? row.OldLine : row.NewLine;

			if (fileLine <= 0)
			{
				return std::nullopt;
			}

			return fileLine;
		}

		void Combine(std::size_t& seed, std::size_t value)
		{
			seed ^= value + kGoldenRatioSeed + (seed << kSeedLeftShift) + (seed >> kSeedRightShift);
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

		const std::int64_t firstLine = std::max<std::int64_t>(selection.First().Line, 0);
		const std::int64_t lastLine =
			std::min<std::int64_t>(selection.Last().Line, static_cast<std::int64_t>(rows.size()) - 1);

		std::vector<Core::SelectableLine> selectableLines;

		for (std::int64_t lineIndex = firstLine; lineIndex <= lastLine; ++lineIndex)
		{
			const std::size_t rowIndex = static_cast<std::size_t>(lineIndex);

			if (!UnifiedLineOf(rows, rowIndex).has_value())
			{
				continue;
			}

			const ResolvedLine resolved =
				ResolveRow(file, rows[rowIndex], DiffSide::New, oldLines, newLines, tokens);

			if (resolved.Text.has_value())
			{
				selectableLines.push_back(Core::SelectableLine{lineIndex, *resolved.Text});
			}
		}

		return Core::JoinSelected(selection, selectableLines);
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

		const std::int64_t firstLine = selection.First().Line;
		const std::int64_t lastLine = selection.Last().Line;

		std::vector<Core::SelectableLine> selectableLines;

		for (const DiffPair& pair : pairs)
		{
			const std::optional<std::int64_t> lineNumber = SideLineOf(pair, side);

			if (!lineNumber.has_value() || *lineNumber < firstLine)
			{
				continue;
			}

			const bool pastSelection = *lineNumber > lastLine;

			if (pastSelection)
			{
				break;
			}

			const DiffRow& row = side == DiffSide::Old ? *pair.Left : *pair.Right;
			const ResolvedLine resolved = ResolveRow(file, row, side, oldLines, newLines, tokens);

			if (resolved.Text.has_value())
			{
				selectableLines.push_back(Core::SelectableLine{*lineNumber, *resolved.Text});
			}
		}

		return Core::JoinSelected(selection, selectableLines);
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

	std::size_t TextsContentHash(const FileTexts& texts)
	{
		std::size_t seed = 0;

		Combine(seed, texts.HasOld ? 1 : 0);
		Combine(seed, std::hash<std::string_view>{}(texts.Old));
		Combine(seed, texts.HasNew ? 1 : 0);
		Combine(seed, std::hash<std::string_view>{}(texts.New));

		return seed;
	}

	std::size_t ByteAtX(std::string_view text, float x,
						const std::function<float(std::string_view prefix)>& widthOf)
	{
		if (text.empty() || x <= 0.0f)
		{
			return 0;
		}

		const std::vector<std::size_t> boundaries = Core::CharacterBoundaries(text);
		const std::size_t nextBoundary = FirstBoundary(boundaries, [&](std::size_t offset) {
			return widthOf(text.substr(0, offset)) >= x;
		});

		if (nextBoundary >= boundaries.size())
		{
			return text.size();
		}

		if (nextBoundary == 0)
		{
			return 0;
		}

		const float widthBefore = widthOf(text.substr(0, boundaries[nextBoundary - 1]));
		const float widthAfter = widthOf(text.substr(0, boundaries[nextBoundary]));

		return x - widthBefore < widthAfter - x ? boundaries[nextBoundary - 1] : boundaries[nextBoundary];
	}

	std::size_t ByteUnderX(std::string_view text, float x,
						   const std::function<float(std::string_view prefix)>& widthOf)
	{
		if (text.empty() || x < 0.0f)
		{
			return 0;
		}

		const std::vector<std::size_t> boundaries = Core::CharacterBoundaries(text);

		const std::size_t characterEnd = FirstBoundary(boundaries, [&](std::size_t offset) {
			return offset > 0 && widthOf(text.substr(0, offset)) > x;
		});

		if (characterEnd >= boundaries.size())
		{
			return text.size();
		}

		return characterEnd == 0 ? 0 : boundaries[characterEnd - 1];
	}
}

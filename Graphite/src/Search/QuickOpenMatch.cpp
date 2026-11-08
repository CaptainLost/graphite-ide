#include "Search/QuickOpenMatch.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr int kMatchScore = 1;
		constexpr int kInFileNameScore = 6;
		constexpr int kWordStartScore = 8;
		constexpr int kNextToPreviousScore = 5;
		constexpr int kFirstOfFileNameScore = 10;
		constexpr int kGapPenalty = 1;

		char Lowered(char character)
		{
			return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
		}

		bool IsSeparator(char character)
		{
			return character == '/' || character == '\\' || character == '_' || character == '-' || character == '.' ||
				   character == ' ';
		}

		bool StartsWord(std::string_view path, std::size_t index)
		{
			if (index == 0)
			{
				return true;
			}

			const char previous = path[index - 1];
			const char here = path[index];
			const bool camelHump = previous >= 'a' && previous <= 'z' && here >= 'A' && here <= 'Z';

			return IsSeparator(previous) || camelHump;
		}
	}

	std::optional<int> ScoreQuickOpenMatch(std::string_view typed, std::string_view path)
	{
		const std::size_t lastSlash = path.find_last_of("/\\");
		const std::size_t nameStart = lastSlash == std::string_view::npos ? 0 : lastSlash + 1;

		// The match is laid from the end, so the typed text's last characters land in the file name when they can.
		std::vector<std::size_t> positions;
		positions.reserve(typed.size());
		std::size_t searchEnd = path.size();

		for (std::size_t typedIndex = typed.size(); typedIndex > 0; --typedIndex)
		{
			const char wanted = Lowered(typed[typedIndex - 1]);

			if (wanted == ' ')
			{
				continue;
			}

			std::size_t found = std::string_view::npos;

			while (searchEnd > 0)
			{
				--searchEnd;

				if (Lowered(path[searchEnd]) == wanted)
				{
					found = searchEnd;
					break;
				}
			}

			if (found == std::string_view::npos)
			{
				return std::nullopt;
			}

			positions.push_back(found);
		}

		std::reverse(positions.begin(), positions.end());

		int score = 0;

		for (std::size_t matchIndex = 0; matchIndex < positions.size(); ++matchIndex)
		{
			const std::size_t at = positions[matchIndex];
			score += kMatchScore;

			if (at >= nameStart)
			{
				score += kInFileNameScore;
			}

			if (at == nameStart)
			{
				score += kFirstOfFileNameScore;
			}

			if (StartsWord(path, at))
			{
				score += kWordStartScore;
			}

			if (matchIndex > 0)
			{
				const std::size_t previous = positions[matchIndex - 1];

				if (at == previous + 1)
				{
					score += kNextToPreviousScore;
				}
				else
				{
					score -= kGapPenalty * static_cast<int>(std::min<std::size_t>(at - previous - 1, 10));
				}
			}
		}

		return score;
	}

	std::vector<std::size_t> RankQuickOpenMatches(std::string_view typed, const std::vector<std::string>& paths,
												  std::size_t limit)
	{
		struct Ranked
		{
			std::size_t Index = 0;
			int Score = 0;
		};

		std::vector<Ranked> ranked;
		const bool nothingTyped = typed.find_first_not_of(' ') == std::string_view::npos;

		for (std::size_t index = 0; index < paths.size(); ++index)
		{
			if (nothingTyped)
			{
				ranked.push_back({.Index = index});
				continue;
			}

			if (const std::optional<int> score = ScoreQuickOpenMatch(typed, paths[index]))
			{
				ranked.push_back({.Index = index, .Score = *score});
			}
		}

		if (!nothingTyped)
		{
			std::stable_sort(ranked.begin(), ranked.end(), [&paths](const Ranked& left, const Ranked& right) {
				if (left.Score != right.Score)
				{
					return left.Score > right.Score;
				}

				return paths[left.Index].size() < paths[right.Index].size();
			});
		}

		if (ranked.size() > limit)
		{
			ranked.resize(limit);
		}

		std::vector<std::size_t> indices;
		indices.reserve(ranked.size());

		for (const Ranked& one : ranked)
		{
			indices.push_back(one.Index);
		}

		return indices;
	}
}

#include "Sessions/PricingPage.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kSectionHeading = "## Model pricing";
		constexpr std::string_view kHeadingMark = "#";

		constexpr std::array<std::string_view, 6> kColumns = {
			"Model", "Base input tokens", "5m cache writes", "1h cache writes", "Cache hits and refreshes", "Output tokens"};

		constexpr std::size_t kInputColumn = 1;
		constexpr std::size_t kCacheWrite5mColumn = 2;
		constexpr std::size_t kCacheWrite1hColumn = 3;
		constexpr std::size_t kCacheReadColumn = 4;
		constexpr std::size_t kOutputColumn = 5;

		constexpr std::string_view kSpaces = " \t";
		constexpr char kCellSeparator = '|';
		constexpr std::string_view kRuleCharacters = ":-";

		constexpr std::string_view kModelPrefix = "claude-";
		constexpr std::string_view kNoteStart = "(";
		constexpr char kIdSeparator = '-';

		constexpr std::string_view kFootnoteStart = "<sup>";
		constexpr std::string_view kDollar = "$";
		constexpr std::string_view kPerMillionTokens = "/ MTok";

		Core::Error PageError(std::string message)
		{
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument, .Message = std::move(message)};
		}

		bool IsTableLine(std::string_view line)
		{
			return Core::WithoutLeading(line, kSpaces).starts_with(kCellSeparator);
		}

		std::vector<std::string_view> CellsOf(std::string_view line)
		{
			std::string_view inner = Core::WithoutSurrounding(line, kSpaces);
			inner.remove_prefix(1);

			if (inner.ends_with(kCellSeparator))
			{
				inner.remove_suffix(1);
			}

			std::vector<std::string_view> cells;
			std::size_t cellStart = 0;

			while (true)
			{
				const std::size_t cellEnd = inner.find(kCellSeparator, cellStart);
				const bool isLastCell = cellEnd == std::string_view::npos;

				cells.push_back(Core::WithoutSurrounding(inner.substr(cellStart, isLastCell ? std::string_view::npos : cellEnd - cellStart), kSpaces));

				if (isLastCell)
				{
					return cells;
				}

				cellStart = cellEnd + 1;
			}
		}

		bool IsRule(const std::vector<std::string_view>& cells)
		{
			return std::ranges::all_of(cells, [](std::string_view cell) {
				return !cell.empty() && cell.find_first_not_of(kRuleCharacters) == std::string_view::npos;
			});
		}

		bool IsIdCharacter(char character)
		{
			return Core::IsAsciiDigit(character) || (character >= 'a' && character <= 'z') || character == kIdSeparator;
		}

		// "Claude Opus 5.5 (note)" is claude-opus-5-5; a name that makes no such id has none.
		std::optional<std::string> ModelIdOf(std::string_view name)
		{
			const std::string_view bareName = Core::WithoutSurrounding(name.substr(0, name.find(kNoteStart)), kSpaces);

			std::string id;
			for (const char character : bareName)
			{
				id += character == ' ' || character == '.' ? kIdSeparator : Core::LowerAscii(character);
			}

			const bool wellFormed = id.starts_with(kModelPrefix) && std::ranges::all_of(id, IsIdCharacter) &&
									!id.ends_with(kIdSeparator) && id.find("--") == std::string::npos;

			return wellFormed ? std::optional<std::string>{std::move(id)} : std::nullopt;
		}

		// "$0.20 / MTok<sup>1</sup>" is 0.20.
		std::optional<double> DollarsPerMillionTokens(std::string_view cell)
		{
			std::string_view price = Core::WithoutSurrounding(cell.substr(0, cell.find(kFootnoteStart)), kSpaces);

			if (!price.starts_with(kDollar) || !price.ends_with(kPerMillionTokens))
			{
				return std::nullopt;
			}

			price.remove_prefix(kDollar.size());
			price.remove_suffix(kPerMillionTokens.size());

			const std::optional<double> dollars = Core::ParseNumber<double>(Core::WithoutSurrounding(price, kSpaces));

			return dollars.has_value() && *dollars >= 0.0 ? dollars : std::nullopt;
		}

		Core::Result<Price> PriceOfRow(const std::vector<std::string_view>& cells)
		{
			std::array<double, kColumns.size()> dollars{};

			for (std::size_t column = kInputColumn; column < kColumns.size(); ++column)
			{
				const std::optional<double> read = DollarsPerMillionTokens(cells[column]);

				if (!read.has_value())
				{
					return PageError(std::format("the {} price of {} ('{}') does not read as dollars per million tokens",
												 Core::Lowered(kColumns[column]), cells[0], cells[column]));
				}

				dollars[column] = *read;
			}

			return Price{.Input = dollars[kInputColumn],
						 .Output = dollars[kOutputColumn],
						 .CacheWrite5m = dollars[kCacheWrite5mColumn],
						 .CacheWrite1h = dollars[kCacheWrite1hColumn],
						 .CacheRead = dollars[kCacheReadColumn]};
		}
	}

	Core::Result<PriceList> ReadPricingPage(std::string_view markdown)
	{
		const std::vector<std::string_view> lines = Core::SplitLines(markdown);

		const auto section = std::ranges::find_if(lines, [](std::string_view line) {
			return Core::WithoutSurrounding(line, kSpaces) == kSectionHeading;
		});

		if (section == lines.end())
		{
			return PageError(std::format("the pricing page has no '{}' section", kSectionHeading.substr(3)));
		}

		auto line = std::next(section);

		while (line != lines.end() && !IsTableLine(*line) && !line->starts_with(kHeadingMark))
		{
			++line;
		}

		if (line == lines.end() || !IsTableLine(*line))
		{
			return PageError("the pricing page's model section has no table");
		}

		if (!std::ranges::equal(CellsOf(*line), kColumns))
		{
			return PageError("the pricing page's model table no longer has the five prices in the order Graphite reads them");
		}

		++line;

		if (line == lines.end() || !IsTableLine(*line) || !IsRule(CellsOf(*line)))
		{
			return PageError("the pricing page's model table has no rule under its header");
		}

		PriceList prices;

		for (++line; line != lines.end() && IsTableLine(*line); ++line)
		{
			const std::vector<std::string_view> cells = CellsOf(*line);

			if (cells.size() != kColumns.size())
			{
				return PageError(std::format("a row of the pricing page's model table has {} cells, not {}", cells.size(), kColumns.size()));
			}

			std::optional<std::string> model = ModelIdOf(cells[0]);

			if (!model.has_value())
			{
				continue;
			}

			Core::Result<Price> price = PriceOfRow(cells);

			if (!price)
			{
				return price.GetError();
			}

			// The first row for a model is its base tier; a later one, such as a dearer price for long prompts, is not kept.
			prices.emplace(std::move(*model), price.GetValue());
		}

		if (prices.empty())
		{
			return PageError("the pricing page's model table names no Claude model");
		}

		return prices;
	}
}

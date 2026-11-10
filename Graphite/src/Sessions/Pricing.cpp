#include "Sessions/Pricing.h"

#include "Core/Base/Strings.h"

#include "Sessions/Requests/RequestTimeline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>

namespace Graphite {

	namespace {
		constexpr double kPerMillion = 1'000'000.0;

		constexpr double kFastMultiplier = 2.0;

		constexpr double kCacheWrite5mInputMultiplier = 1.25;
		constexpr double kCacheWrite1hInputMultiplier = 2.0;

		constexpr std::string_view kSyntheticModel = "<synthetic>";

		constexpr std::string_view kModelPrefix = "claude-";

		constexpr std::size_t kDateSuffixLength = 9;

		constexpr char kPriceFieldSeparator = ',';

		constexpr std::size_t kPriceFieldCount = 5;

		bool IsDateSuffix(std::string_view rest)
		{
			if (rest.size() != kDateSuffixLength || rest[0] != '-')
			{
				return false;
			}

			for (const char character : rest.substr(1))
			{
				if (std::isdigit(static_cast<unsigned char>(character)) == 0)
				{
					return false;
				}
			}

			return true;
		}

		bool NamesModel(std::string_view id, std::string_view model)
		{
			if (!id.starts_with(model))
			{
				return false;
			}

			const std::string_view rest = id.substr(model.size());

			return rest.empty() || rest.front() == '[' || IsDateSuffix(rest);
		}

		bool ListsModel(const std::vector<std::string>& models, const std::string& model)
		{
			return std::find(models.begin(), models.end(), model) != models.end();
		}

		double DollarsForTokens(std::uint64_t tokens, double dollarsPerMillion, double speedScale)
		{
			return static_cast<double>(tokens) / kPerMillion * dollarsPerMillion * speedScale;
		}
	}

	bool IsSyntheticModel(std::string_view model)
	{
		return model == kSyntheticModel;
	}

	Price StandardPrice(double input, double output, double cacheRead)
	{
		return {input, output, input * kCacheWrite5mInputMultiplier, input * kCacheWrite1hInputMultiplier, cacheRead};
	}

	Pricing Pricing::Defaults()
	{
		Pricing pricing;

		// WARNING: Older models are deliberately absent. An absent model shows as "?" and can be
		// priced in settings, where a guessed price would show a wrong number with nothing to say it was guessed.
		// WARNING: Haiku 5.5's cache read is assumed at a tenth of its input price, and its dearer tier past 100K
		// input tokens ($0.50 / $2.50) is not modelled, so a request that long is priced low.
		pricing.m_Defaults = {
			{"claude-fable-5-2", StandardPrice(8.0, 40.0, 0.20)},
			{"claude-fable-5-1", StandardPrice(10.0, 50.0, 0.25)},
			{"claude-fable-5", StandardPrice(10.0, 50.0, 1.0)},
			{"claude-mythos-5", StandardPrice(10.0, 50.0, 1.0)},
			{"claude-opus-5-5", StandardPrice(4.0, 20.0, 0.20)},
			{"claude-opus-5", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-opus-4-8", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-opus-4-7", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-opus-4-6", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-sonnet-5-5", StandardPrice(2.0, 10.0, 0.10)},
			{"claude-sonnet-5", StandardPrice(2.0, 10.0, 0.2)},
			{"claude-sonnet-4-6", StandardPrice(3.0, 15.0, 0.3)},
			{"claude-haiku-5-5", StandardPrice(0.10, 0.50, 0.01)},
			{"claude-haiku-4-5", StandardPrice(1.0, 5.0, 0.1)},
		};

		pricing.m_Prices = pricing.m_Defaults;
		return pricing;
	}

	void Pricing::Set(std::string model, Price price)
	{
		m_Prices[std::move(model)] = price;
	}

	void Pricing::Reset(const std::string& model)
	{
		if (const std::map<std::string, Price>::const_iterator found = m_Defaults.find(model); found != m_Defaults.end())
		{
			const auto& [defaultModel, defaultPrice] = *found;
			m_Prices[model] = defaultPrice;
		}
		else
		{
			m_Prices.erase(model);
		}
	}

	const Price* Pricing::Find(std::string_view model) const
	{
		const Price* bestMatch = nullptr;
		std::size_t bestNameLength = 0;

		for (const auto& [name, price] : m_Prices)
		{
			if (NamesModel(model, name) && name.size() > bestNameLength)
			{
				bestMatch = &price;
				bestNameLength = name.size();
			}
		}

		return bestMatch;
	}

	std::optional<BucketCosts> Pricing::BucketsOf(const UsageKey& key, const TokenCounts& counts) const
	{
		// WARNING: Claude Code names this model on messages it writes itself, such as an interrupted
		// turn. Nothing was billed for them, so the cost is zero rather than unknown.
		if (IsSyntheticModel(key.Model))
		{
			return BucketCosts{};
		}

		const Price* price = Find(key.Model);

		if (price == nullptr)
		{
			return std::nullopt;
		}

		const double speedScale = key.Fast ? kFastMultiplier : 1.0;

		BucketCosts costs;
		costs.Input = DollarsForTokens(counts.Input, price->Input, speedScale);
		costs.Output = DollarsForTokens(counts.Output, price->Output, speedScale);
		costs.CacheWrite = DollarsForTokens(counts.CacheWrite5m, price->CacheWrite5m, speedScale) +
						   DollarsForTokens(counts.CacheWrite1h, price->CacheWrite1h, speedScale);
		costs.CacheRead = DollarsForTokens(counts.CacheRead, price->CacheRead, speedScale);
		return costs;
	}

	CostResult& CostResult::operator+=(const CostResult& other)
	{
		Dollars += other.Dollars;

		for (const std::string& model : other.UnknownModels)
		{
			if (!ListsModel(UnknownModels, model))
			{
				UnknownModels.push_back(model);
			}
		}

		return *this;
	}

	std::string ModelLabel(std::string_view model, bool fast)
	{
		std::string label{model.starts_with(kModelPrefix) ? model.substr(kModelPrefix.size()) : model};

		if (fast)
		{
			label += " fast";
		}

		return label;
	}

	CostResult Pricing::CostOf(const ApiRequest& request) const
	{
		return CostOf(UsageKey{request.Model, request.Fast}, request.Usage);
	}

	CostResult Pricing::CostOf(const UsageKey& key, const TokenCounts& counts) const
	{
		return CostOf(UsageByModel{{key, counts}});
	}

	CostResult Pricing::CostOf(const UsageByModel& usage) const
	{
		CostResult result;

		for (const auto& [key, counts] : usage)
		{
			if (counts.Total() == 0)
			{
				continue;
			}

			if (const std::optional<BucketCosts> costs = BucketsOf(key, counts))
			{
				result.Dollars += costs->Total();
			}
			else if (!ListsModel(result.UnknownModels, key.Model))
			{
				result.UnknownModels.push_back(key.Model);
			}
		}

		return result;
	}

	std::optional<Price> ParsePrice(std::string_view text)
	{
		std::array<double, kPriceFieldCount> fieldValues{};
		std::size_t fieldIndex = 0;
		std::size_t fieldStart = 0;

		while (fieldIndex < fieldValues.size())
		{
			const std::size_t fieldEnd = std::min(text.find(kPriceFieldSeparator, fieldStart), text.size());
			const std::string_view field = text.substr(fieldStart, fieldEnd - fieldStart);

			const std::optional<double> parsedField = Core::ParseNumber<double>(field);

			if (!parsedField || *parsedField < 0.0)
			{
				return std::nullopt;
			}

			fieldValues[fieldIndex] = *parsedField;
			++fieldIndex;

			if (fieldEnd == text.size())
			{
				break;
			}

			fieldStart = fieldEnd + 1;
		}

		if (fieldIndex != fieldValues.size())
		{
			return std::nullopt;
		}

		return Price{.Input = fieldValues[0],
					 .Output = fieldValues[1],
					 .CacheWrite5m = fieldValues[2],
					 .CacheWrite1h = fieldValues[3],
					 .CacheRead = fieldValues[4]};
	}

	std::string FormatPrice(const Price& price)
	{
		return std::format("{:g},{:g},{:g},{:g},{:g}", price.Input, price.Output, price.CacheWrite5m,
						   price.CacheWrite1h, price.CacheRead);
	}
}

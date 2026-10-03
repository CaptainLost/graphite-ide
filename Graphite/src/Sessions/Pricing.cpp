#include "Sessions/Pricing.h"

#include "Sessions/Requests/RequestTimeline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>

namespace Graphite {

	namespace {
		constexpr double kPerMillion = 1'000'000.0;

		// Fast mode is priced at twice the standard rate on every model that offers it.
		constexpr double kFastMultiplier = 2.0;

		bool IsDateSuffix(std::string_view rest)
		{
			if (rest.size() != 9 || rest[0] != '-')
			{
				return false;
			}

			for (const char c : rest.substr(1))
			{
				if (std::isdigit(static_cast<unsigned char>(c)) == 0)
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
	}

	Price StandardPrice(double input, double output, double cacheRead)
	{
		return {input, output, input * 1.25, input * 2.0, cacheRead};
	}

	Pricing Pricing::Defaults()
	{
		Pricing pricing;

		// First-party API list prices, dollars per million tokens. Cache reads are 0.1× input
		// except where a model's own rate differs. Older models are deliberately absent: an
		// absent model shows as "?" and can be priced in settings, where a guessed price would
		// show a wrong number with nothing to say it was guessed.
		pricing.m_Defaults = {
			{"claude-fable-5-1", StandardPrice(10.0, 50.0, 0.25)},
			{"claude-fable-5", StandardPrice(10.0, 50.0, 1.0)},
			{"claude-mythos-5", StandardPrice(10.0, 50.0, 1.0)},
			{"claude-opus-5-5", StandardPrice(4.0, 20.0, 0.20)},
			{"claude-opus-5", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-opus-4-8", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-opus-4-7", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-opus-4-6", StandardPrice(5.0, 25.0, 0.5)},
			{"claude-sonnet-5", StandardPrice(2.0, 10.0, 0.2)},
			{"claude-sonnet-4-6", StandardPrice(3.0, 15.0, 0.3)},
			{"claude-haiku-4-5", StandardPrice(1.0, 5.0, 0.1)},
			// What Claude Code names on messages it writes itself — an interrupted turn, an
			// API error shown in the conversation. Nothing was billed for them.
			{"<synthetic>", Price{}},
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
		if (const auto found = m_Defaults.find(model); found != m_Defaults.end())
		{
			m_Prices[model] = found->second;
		}
		else
		{
			m_Prices.erase(model);
		}
	}

	const Price* Pricing::Find(std::string_view model) const
	{
		const Price* best = nullptr;
		std::size_t bestLength = 0;

		for (const auto& [name, price] : m_Prices)
		{
			if (NamesModel(model, name) && name.size() > bestLength)
			{
				best = &price;
				bestLength = name.size();
			}
		}

		return best;
	}

	std::optional<BucketCosts> Pricing::BucketsOf(const UsageKey& key, const TokenCounts& counts) const
	{
		const Price* price = Find(key.Model);

		if (price == nullptr)
		{
			return std::nullopt;
		}

		const double scale = key.Fast ? kFastMultiplier : 1.0;
		const auto dollars = [scale](std::uint64_t tokens, double perMillion) { return static_cast<double>(tokens) / kPerMillion * perMillion * scale; };

		BucketCosts costs;
		costs.Input = dollars(counts.Input, price->Input);
		costs.Output = dollars(counts.Output, price->Output);
		costs.CacheWrite = dollars(counts.CacheWrite5m, price->CacheWrite5m) +
						   dollars(counts.CacheWrite1h, price->CacheWrite1h);
		costs.CacheRead = dollars(counts.CacheRead, price->CacheRead);
		return costs;
	}

	CostResult& CostResult::operator+=(const CostResult& other)
	{
		Dollars += other.Dollars;

		for (const std::string& model : other.UnknownModels)
		{
			if (std::find(UnknownModels.begin(), UnknownModels.end(), model) == UnknownModels.end())
			{
				UnknownModels.push_back(model);
			}
		}

		return *this;
	}

	std::string ModelLabel(std::string_view model, bool fast)
	{
		std::string label{model.starts_with("claude-") ? model.substr(7) : model};

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
			else if (std::find(result.UnknownModels.begin(), result.UnknownModels.end(), key.Model) ==
					 result.UnknownModels.end())
			{
				result.UnknownModels.push_back(key.Model);
			}
		}

		return result;
	}

	std::optional<Price> ParsePrice(std::string_view text)
	{
		std::array<double, 5> values{};
		std::size_t index = 0;
		std::size_t start = 0;

		while (index < values.size())
		{
			const std::size_t end = std::min(text.find(',', start), text.size());
			const std::string_view part = text.substr(start, end - start);

			const auto [next, error] = std::from_chars(part.data(), part.data() + part.size(), values[index]);

			if (error != std::errc{} || next != part.data() + part.size() || values[index] < 0.0)
			{
				return std::nullopt;
			}

			++index;

			if (end == text.size())
			{
				break;
			}

			start = end + 1;
		}

		if (index != values.size())
		{
			return std::nullopt;
		}

		return Price{values[0], values[1], values[2], values[3], values[4]};
	}

	std::string FormatPrice(const Price& price)
	{
		return std::format("{:g},{:g},{:g},{:g},{:g}", price.Input, price.Output, price.CacheWrite5m,
						   price.CacheWrite1h, price.CacheRead);
	}
}

#pragma once

#include "Sessions/Transcript/TranscriptRecord.h"

#include <compare>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct Price
	{
		double Input = 0.0;
		double Output = 0.0;
		double CacheWrite5m = 0.0;
		double CacheWrite1h = 0.0;
		double CacheRead = 0.0;

		friend bool operator==(const Price&, const Price&) = default;
	};

	Price StandardPrice(double input, double output, double cacheRead);

	struct UsageKey
	{
		std::string Model;
		bool Fast = false;

		std::strong_ordering operator<=>(const UsageKey&) const = default;
	};

	using UsageByModel = std::map<UsageKey, TokenCounts>;

	struct CostResult
	{
		double Dollars = 0.0;

		std::vector<std::string> UnknownModels;

		bool IsKnown() const { return UnknownModels.empty(); }

		CostResult& operator+=(const CostResult& other);
	};

	struct ApiRequest;

	std::string ModelLabel(std::string_view model, bool fast);

	struct BucketCosts
	{
		double Input = 0.0;
		double Output = 0.0;
		double CacheWrite = 0.0;
		double CacheRead = 0.0;

		double Total() const { return Input + Output + CacheWrite + CacheRead; }
	};

	class Pricing
	{
	public:
		static Pricing Defaults();

		void Set(std::string model, Price price);

		void Reset(const std::string& model);

		// WARNING: Only the id itself, the id with a date suffix (`-20251001`) or with a bracketed
		// variant (`[1m]`) match. Nothing looser: `claude-opus-5-7` is not `claude-opus-5`, and pricing it
		// as one would give a number that looks right and is not.
		const Price* Find(std::string_view model) const;

		CostResult CostOf(const UsageByModel& usage) const;

		CostResult CostOf(const ApiRequest& request) const;

		CostResult CostOf(const UsageKey& key, const TokenCounts& counts) const;

		std::optional<BucketCosts> BucketsOf(const UsageKey& key, const TokenCounts& counts) const;

		const std::map<std::string, Price>& GetEntries() const { return m_Prices; }

	private:
		std::map<std::string, Price> m_Prices;
		std::map<std::string, Price> m_Defaults;
	};

	std::optional<Price> ParsePrice(std::string_view text);
	std::string FormatPrice(const Price& price);
}

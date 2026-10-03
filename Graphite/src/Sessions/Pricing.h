#pragma once

#include "Sessions/Transcript/TranscriptRecord.h"

#include <compare>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// Dollars per million tokens, one figure per bucket.
	struct Price
	{
		double Input = 0.0;
		double Output = 0.0;
		double CacheWrite5m = 0.0;
		double CacheWrite1h = 0.0;
		double CacheRead = 0.0;

		friend bool operator==(const Price&, const Price&) = default;
	};

	// The usual shape of a price: cache writes at 1.25× input for five minutes and 2× for an
	// hour. Cache reads are given, because they are the figure that differs between models.
	Price StandardPrice(double input, double output, double cacheRead);

	// Usage is kept apart by model and by speed, because a fast request of the same model is
	// billed at a different rate.
	struct UsageKey
	{
		std::string Model;
		bool Fast = false;

		auto operator<=>(const UsageKey&) const = default;
	};

	using UsageByModel = std::map<UsageKey, TokenCounts>;

	struct CostResult
	{
		double Dollars = 0.0;

		// Models that had tokens and no price. Their share is missing from Dollars, which is
		// why a caller shows "?" rather than the number while this is not empty.
		std::vector<std::string> UnknownModels;

		bool IsKnown() const { return UnknownModels.empty(); }

		// Adds another cost in: the dollars sum, and a model unknown to either side stays
		// unknown. A model named twice is listed once.
		CostResult& operator+=(const CostResult& other);
	};

	struct ApiRequest;

	// How a model is named where a request is shown: the id without its `claude-` prefix, and
	// " fast" after it for a fast request. Truncation is left to the caller.
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

		// Back to the compiled-in price, or to no price for a model that had none.
		void Reset(const std::string& model);

		// The price for a model id as a transcript writes it: the id itself, the id with a
		// date suffix (`-20251001`), or the id with a bracketed variant (`[1m]`). Nothing
		// looser — `claude-opus-5-7` is not `claude-opus-5`, and pricing it as one would be a
		// number that looks right and is not.
		const Price* Find(std::string_view model) const;

		CostResult CostOf(const UsageByModel& usage) const;

		// What one request cost: the same figure CostOf makes of its usage under its model and speed.
		CostResult CostOf(const ApiRequest& request) const;

		// The same for one model and speed's usage, for a caller that holds them apart.
		CostResult CostOf(const UsageKey& key, const TokenCounts& counts) const;

		std::optional<BucketCosts> BucketsOf(const UsageKey& key, const TokenCounts& counts) const;

		const std::map<std::string, Price>& GetEntries() const { return m_Prices; }

	private:
		std::map<std::string, Price> m_Prices;
		std::map<std::string, Price> m_Defaults;
	};

	// "input,output,write5m,write1h,read" — how an override is stored in the settings file.
	std::optional<Price> ParsePrice(std::string_view text);
	std::string FormatPrice(const Price& price);
}

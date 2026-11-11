#pragma once

#include "Sessions/Transcript/TranscriptRecord.h"

#include <compare>
#include <cstdint>
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

	using PriceList = std::map<std::string, Price>;

	// Where a model's price comes from, the later winning: the list built into Graphite, the pricing page as last
	// fetched, then an edit in settings.
	enum class PriceSource : std::uint8_t
	{
		BuiltIn,
		Fetched,
		Edited
	};

	// WARNING: Claude Code names this model on messages it writes itself; they reach no model and cost nothing.
	bool IsSyntheticModel(std::string_view model);

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

		// An edit: it wins over the built-in and the fetched price, and a later fetch leaves it alone.
		void Set(std::string model, Price price);

		// Drops the edit, so the fetched price, or failing that the built-in one, shows again.
		void Reset(const std::string& model);

		// Replaces the whole fetched layer: a model the newer list no longer names falls back to its built-in price.
		void SetFetched(PriceList prices);

		std::optional<PriceSource> SourceOf(const std::string& model) const;

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
		void Rebuild();

		PriceList m_Prices;
		PriceList m_Defaults;
		PriceList m_Fetched;
		PriceList m_Edited;
	};

	std::optional<Price> ParsePrice(std::string_view text);
	std::string FormatPrice(const Price& price);
}

#include "Sessions/ClaudeSettings.h"

#include "Sessions/ClaudeLaunch.h"
#include "Settings/AppSettings.h"

#include "Core/Platform/ShellProfiles.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	void ClaudeSettings::Load()
	{
		m_Binary = m_Store.GetClaudeBinary();
		m_Pricing = Pricing::Defaults();
		m_PricesFetchedOn = m_Store.GetClaudePricesFetchedOn();

		// WARNING: A price that does not parse is ignored rather than zeroed, because zero would show a
		// confident, wrong cost.
		PriceList fetched;

		for (const auto& [model, text] : m_Store.GetClaudeFetchedPrices())
		{
			if (std::optional<Price> price = ParsePrice(text))
			{
				fetched.emplace(model, *price);
			}
		}

		m_Pricing.SetFetched(std::move(fetched));

		for (const auto& [model, text] : m_Store.GetClaudePrices())
		{
			if (std::optional<Price> price = ParsePrice(text))
			{
				m_Pricing.Set(model, *price);
			}
		}
	}

	void ClaudeSettings::AdoptFetchedPrices(const PriceList& prices, std::string_view fetchedOn)
	{
		std::vector<std::pair<std::string, std::string>> stored;
		stored.reserve(prices.size());

		for (const auto& [model, price] : prices)
		{
			stored.emplace_back(model, FormatPrice(price));
		}

		m_Store.ReplaceClaudeFetchedPrices(stored);
		m_Store.SetClaudePricesFetchedOn(fetchedOn);

		m_Pricing.SetFetched(prices);
		m_PricesFetchedOn = fetchedOn;
	}

	void ClaudeSettings::SetBinary(std::filesystem::path binary)
	{
		m_Binary = std::move(binary);
		m_Store.SetClaudeBinary(m_Binary);
	}

	void ClaudeSettings::SetPrice(const std::string& model, const Price& price)
	{
		m_Pricing.Set(model, price);
		m_Store.SetClaudePrice(model, FormatPrice(price));
	}

	void ClaudeSettings::ResetPrice(const std::string& model)
	{
		m_Pricing.Reset(model);
		m_Store.RemoveClaudePrice(model);
	}

	std::filesystem::path ClaudeSettings::ResolveProgram() const
	{
		return ResolveClaudeProgram(m_Binary, [](const std::string& name) { return Core::FindOnPath(name); });
	}
}

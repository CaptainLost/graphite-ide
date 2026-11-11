#pragma once

#include "Sessions/Pricing.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	class AppSettings;

	class ClaudeSettings
	{
	public:
		explicit ClaudeSettings(AppSettings& settings) : m_Store(settings) {}

		void Load();

		const std::filesystem::path& GetBinary() const { return m_Binary; }
		void SetBinary(std::filesystem::path binary);

		const Pricing& GetPricing() const { return m_Pricing; }
		void SetPrice(const std::string& model, const Price& price);
		void ResetPrice(const std::string& model);

		// Replaces the prices last fetched, and the day they were, keeping every edit.
		void AdoptFetchedPrices(const PriceList& prices, std::string_view fetchedOn);

		// Empty when the pricing page was never fetched.
		const std::string& GetPricesFetchedOn() const { return m_PricesFetchedOn; }

		std::filesystem::path ResolveProgram() const;

	private:
		AppSettings& m_Store;
		std::filesystem::path m_Binary;
		Pricing m_Pricing = Pricing::Defaults();
		std::string m_PricesFetchedOn;
	};
}

#pragma once

#include "Sessions/Pricing.h"

#include <filesystem>
#include <string>

namespace Graphite {

	class AppSettings;

	// What the user configured about Claude: which program to run and what each model costs.
	// Changes go to AppSettings at once, as every other setting here does.
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

		// The program "New Claude session" runs; empty when there is none.
		std::filesystem::path ResolveProgram() const;

	private:
		AppSettings& m_Store;
		std::filesystem::path m_Binary;
		Pricing m_Pricing = Pricing::Defaults();
	};
}

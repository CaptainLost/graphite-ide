#pragma once

#include "Sessions/Pricing.h"

#include <filesystem>
#include <string>

namespace Graphite {

	// What the user configured about Claude: which program to run and what each model costs.
	// Changes persist at once, as every other setting here does.
	class ClaudeSettings
	{
	public:
		void Load();

		const std::filesystem::path& GetBinary() const { return m_Binary; }
		void SetBinary(std::filesystem::path binary);

		const Pricing& GetPricing() const { return m_Pricing; }
		void SetPrice(const std::string& model, const Price& price);
		void ResetPrice(const std::string& model);

		// The program "New Claude session" runs; empty when there is none.
		std::filesystem::path ResolveProgram() const;

	private:
		std::filesystem::path m_Binary;
		Pricing m_Pricing = Pricing::Defaults();
	};
}

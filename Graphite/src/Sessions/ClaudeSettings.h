#pragma once

#include "Sessions/Pricing.h"

#include <filesystem>
#include <string>

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

		std::filesystem::path ResolveProgram() const;

	private:
		AppSettings& m_Store;
		std::filesystem::path m_Binary;
		Pricing m_Pricing = Pricing::Defaults();
	};
}

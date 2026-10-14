#pragma once

#include <cstddef>
#include <optional>

namespace Graphite {

	class AppSettings;

	class AnalysisSettings
	{
	public:
		static constexpr std::size_t kMinimumBudgetMegabytes = 16;
		static constexpr std::size_t kMaximumBudgetMegabytes = 4096;
		static constexpr std::size_t kDefaultBudgetMegabytes = 128;

		static constexpr std::size_t kBytesPerMegabyte = 1024ull * 1024ull;

		static constexpr std::size_t kMinimumConcurrency = 1;
		static constexpr std::size_t kMaximumConcurrency = 8;

		explicit AnalysisSettings(AppSettings& settings) : m_Store(settings) {}

		void Load();

		bool IsEnabled() const { return m_Enabled; }

		std::size_t GetBudgetMegabytes() const { return m_BudgetMegabytes; }

		std::size_t GetBudgetBytes() const { return m_BudgetMegabytes * kBytesPerMegabyte; }

		void SetEnabled(bool enabled);

		void SetBudgetMegabytes(std::size_t megabytes);

		static std::size_t AutomaticConcurrency();

		bool IsConcurrencyAutomatic() const { return m_ConcurrencyAutomatic; }

		std::size_t GetConcurrency() const;

		void SetConcurrencyAutomatic(bool automatic);

		void SetConcurrency(std::size_t concurrency);

	private:
		AppSettings& m_Store;

		bool m_Enabled = true;
		std::size_t m_BudgetMegabytes = kDefaultBudgetMegabytes;

		bool m_ConcurrencyAutomatic = true;
		std::size_t m_Concurrency = kMinimumConcurrency;

		std::optional<std::size_t> m_ConcurrencyChosenThisSession;
	};
}

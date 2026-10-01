#pragma once

#include <cstddef>
#include <optional>

namespace Graphite {

	class AppSettings;

	// One owner of the two persisted background-analysis settings. The section edits it and
	// GraphiteLayer reads it; without this they would each hold their own copy and drift apart the
	// first time one of them forgot to reload. The same reason SearchSettings exists next door.
	class AnalysisSettings
	{
	public:
		static constexpr std::size_t kMinimumBudgetMegabytes = 16;
		static constexpr std::size_t kMaximumBudgetMegabytes = 4096;
		static constexpr std::size_t kDefaultBudgetMegabytes = 128;

		static constexpr std::size_t kMinimumConcurrency = 1;
		static constexpr std::size_t kMaximumConcurrency = 8;

		explicit AnalysisSettings(AppSettings& settings) : m_Store(settings) {}

		void Load();

		bool IsEnabled() const { return m_Enabled; }

		std::size_t GetBudgetMegabytes() const { return m_BudgetMegabytes; }

		std::size_t GetBudgetBytes() const { return m_BudgetMegabytes * 1024ull * 1024ull; }

		void SetEnabled(bool enabled);

		void SetBudgetMegabytes(std::size_t megabytes);

		// What this machine is worth: a quarter of its cores, never fewer than two and never more
		// than six. A quarter because each outstanding file is a translation unit clangd is
		// parsing, and the point is to fill the spare capacity of a machine somebody is working
		// on, not to take it over. The floor keeps a small machine from serialising to nothing;
		// the ceiling keeps a very large one from drowning the server in preambles for a pass
		// nobody is waiting on.
		static std::size_t AutomaticConcurrency();

		bool IsConcurrencyAutomatic() const { return m_ConcurrencyAutomatic; }

		// The automatic figure while that is what is wanted, and the stored one otherwise, so a
		// caller never has to ask which mode this is in.
		std::size_t GetConcurrency() const;

		void SetConcurrencyAutomatic(bool automatic);

		void SetConcurrency(std::size_t concurrency);

	private:
		AppSettings& m_Store;

		bool m_Enabled = true;
		std::size_t m_BudgetMegabytes = kDefaultBudgetMegabytes;

		bool m_ConcurrencyAutomatic = true;
		std::size_t m_Concurrency = kMinimumConcurrency;

		// The last number somebody actually chose, kept for the life of the process even while
		// automatic is on, so ticking the box back off returns what was chosen rather than
		// reseeding from the machine every time. Not persisted: on disk automatic is still the
		// absent/zero value, so a toggle-off in a later session — after this has gone with the
		// process — falls back to AutomaticConcurrency() instead. That is the lesser loss: a
		// sentinel here cannot tell "never chosen" from "chose kMinimumConcurrency", so the
		// distinction can only be carried in memory, not in the stored number itself.
		std::optional<std::size_t> m_RememberedConcurrency;
	};
}

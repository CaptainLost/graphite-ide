#include "Settings/AnalysisSettings.h"

#include "Settings/AppSettings.h"

#include <algorithm>
#include <thread>

namespace Graphite {

	void AnalysisSettings::Load()
	{
		m_Enabled = AppSettings::LoadBackgroundAnalysis();

		const std::size_t saved = AppSettings::LoadAnalysisBudgetMegabytes();

		m_BudgetMegabytes = saved == 0
								 ? kDefaultBudgetMegabytes
								 : std::clamp(saved, kMinimumBudgetMegabytes, kMaximumBudgetMegabytes);

		const std::size_t savedConcurrency = AppSettings::LoadAnalysisConcurrency();

		m_ConcurrencyAutomatic = savedConcurrency == 0;

		if (!m_ConcurrencyAutomatic)
		{
			m_Concurrency = std::clamp(savedConcurrency, kMinimumConcurrency, kMaximumConcurrency);
		}
	}

	void AnalysisSettings::SetEnabled(bool enabled)
	{
		if (m_Enabled == enabled)
		{
			return;
		}

		m_Enabled = enabled;
		AppSettings::SaveBackgroundAnalysis(enabled);
	}

	void AnalysisSettings::SetBudgetMegabytes(std::size_t megabytes)
	{
		const std::size_t clamped = std::clamp(megabytes, kMinimumBudgetMegabytes, kMaximumBudgetMegabytes);

		if (m_BudgetMegabytes == clamped)
		{
			return;
		}

		m_BudgetMegabytes = clamped;
		AppSettings::SaveAnalysisBudgetMegabytes(clamped);
	}

	std::size_t AnalysisSettings::AutomaticConcurrency()
	{
		// hardware_concurrency() is allowed to return 0 when it cannot tell; that is "cannot
		// tell", not "no cores", so it falls back to the floor rather than dividing by four and
		// clamping a zero up to the same floor by accident.
		const unsigned int cores = std::thread::hardware_concurrency();

		if (cores == 0)
		{
			return kMinimumConcurrency;
		}

		return std::clamp<std::size_t>(cores / 4, 2, 6);
	}

	std::size_t AnalysisSettings::GetConcurrency() const
	{
		return m_ConcurrencyAutomatic ? AutomaticConcurrency() : m_Concurrency;
	}

	void AnalysisSettings::SetConcurrencyAutomatic(bool automatic)
	{
		if (m_ConcurrencyAutomatic == automatic)
		{
			return;
		}

		m_ConcurrencyAutomatic = automatic;

		if (!automatic)
		{
			// Seed the stored figure from what automatic was actually giving, so unticking the
			// box means "keep doing what you were doing, but let me change it" rather than
			// silently dropping to the minimum m_Concurrency happens to hold until somebody
			// drags the slider.
			m_Concurrency = AutomaticConcurrency();
		}

		// Zero means automatic to AppSettings, the same idiom the budget uses for "absent".
		AppSettings::SaveAnalysisConcurrency(automatic ? 0 : m_Concurrency);
	}

	void AnalysisSettings::SetConcurrency(std::size_t concurrency)
	{
		const std::size_t clamped = std::clamp(concurrency, kMinimumConcurrency, kMaximumConcurrency);

		if (!m_ConcurrencyAutomatic && m_Concurrency == clamped)
		{
			return;
		}

		m_ConcurrencyAutomatic = false;
		m_Concurrency = clamped;
		AppSettings::SaveAnalysisConcurrency(clamped);
	}
}

#include "Settings/AnalysisSettings.h"

#include "Settings/AppSettings.h"

#include <algorithm>
#include <thread>

namespace Graphite {

	namespace {
		constexpr std::size_t kAbsentFigure = 0;
		constexpr unsigned int kCoreCountUnknown = 0;
		constexpr unsigned int kCoresPerAnalysisFile = 4;
		constexpr std::size_t kMinimumAutomaticConcurrency = 2;
		constexpr std::size_t kMaximumAutomaticConcurrency = 6;
	}

	void AnalysisSettings::Load()
	{
		m_Enabled = m_Store.IsBackgroundAnalysisEnabled();

		const std::size_t savedBudget = m_Store.GetAnalysisBudgetMegabytes();

		m_BudgetMegabytes = savedBudget == kAbsentFigure
								? kDefaultBudgetMegabytes
								: std::clamp(savedBudget, kMinimumBudgetMegabytes, kMaximumBudgetMegabytes);

		const std::size_t savedConcurrency = m_Store.GetAnalysisConcurrency();

		m_ConcurrencyAutomatic = savedConcurrency == kAbsentFigure;

		if (!m_ConcurrencyAutomatic)
		{
			m_Concurrency = std::clamp(savedConcurrency, kMinimumConcurrency, kMaximumConcurrency);
			m_ConcurrencyChosenThisSession = m_Concurrency;
		}
	}

	void AnalysisSettings::SetEnabled(bool enabled)
	{
		if (m_Enabled == enabled)
		{
			return;
		}

		m_Enabled = enabled;
		m_Store.SetBackgroundAnalysis(enabled);
	}

	void AnalysisSettings::SetBudgetMegabytes(std::size_t megabytes)
	{
		const std::size_t clampedMegabytes = std::clamp(megabytes, kMinimumBudgetMegabytes, kMaximumBudgetMegabytes);

		if (m_BudgetMegabytes == clampedMegabytes)
		{
			return;
		}

		m_BudgetMegabytes = clampedMegabytes;
		m_Store.SetAnalysisBudgetMegabytes(clampedMegabytes);
	}

	std::size_t AnalysisSettings::AutomaticConcurrency()
	{
		// WARNING: hardware_concurrency() returns 0 when it cannot tell, which is not "no cores".
		const unsigned int coreCount = std::thread::hardware_concurrency();

		if (coreCount == kCoreCountUnknown)
		{
			return kMinimumConcurrency;
		}

		return std::clamp<std::size_t>(coreCount / kCoresPerAnalysisFile, kMinimumAutomaticConcurrency, kMaximumAutomaticConcurrency);
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
			m_Concurrency = m_ConcurrencyChosenThisSession.value_or(AutomaticConcurrency());
		}

		m_Store.SetAnalysisConcurrency(automatic ? kAbsentFigure : m_Concurrency);
	}

	void AnalysisSettings::SetConcurrency(std::size_t concurrency)
	{
		const std::size_t clampedConcurrency = std::clamp(concurrency, kMinimumConcurrency, kMaximumConcurrency);

		m_ConcurrencyChosenThisSession = clampedConcurrency;

		if (!m_ConcurrencyAutomatic && m_Concurrency == clampedConcurrency)
		{
			return;
		}

		m_ConcurrencyAutomatic = false;
		m_Concurrency = clampedConcurrency;
		m_Store.SetAnalysisConcurrency(clampedConcurrency);
	}
}

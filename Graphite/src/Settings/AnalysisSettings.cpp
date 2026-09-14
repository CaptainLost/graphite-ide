#include "Settings/AnalysisSettings.h"

#include "Settings/AppSettings.h"

#include <algorithm>

namespace Graphite {

	void AnalysisSettings::Load()
	{
		m_Enabled = AppSettings::LoadBackgroundAnalysis();

		const std::size_t saved = AppSettings::LoadAnalysisBudgetMegabytes();

		m_BudgetMegabytes = saved == 0
								 ? kDefaultBudgetMegabytes
								 : std::clamp(saved, kMinimumBudgetMegabytes, kMaximumBudgetMegabytes);
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
}

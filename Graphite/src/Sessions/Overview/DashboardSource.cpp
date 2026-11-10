#include "Sessions/Overview/DashboardSource.h"

#include "Sessions/Overview/TipText.h"
#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <unordered_map>

namespace Graphite {

	namespace {
		constexpr std::string_view kClaudeFolder = ".claude";
		constexpr std::string_view kWorktreesFolder = "worktrees";

		bool HasUsage(const DayTotals& day)
		{
			return day.Requests > 0 || day.Cost > 0.0;
		}
	}

	bool IsPlaceUnder(const SessionPlace& place, const std::filesystem::path& folder)
	{
		if (place.Worktree.empty())
		{
			return IsWithinDirectory(place.Repository, folder);
		}

		const std::filesystem::path worktree =
			place.Repository / kClaudeFolder / kWorktreesFolder / Core::FromUtf8(place.Worktree);
		return IsWithinDirectory(worktree, folder);
	}

	void DashboardSource::MarkChanged()
	{
		m_Changed = true;
	}

	bool DashboardSource::Refresh(const SessionsByFile& sessions, const Pricing& pricing,
								  const std::filesystem::path& folder, const LocalCalendar& calendar,
								  std::chrono::year_month_day today)
	{
		if (!m_Changed && m_Prices == pricing.GetEntries() && m_Folder == folder && m_Today == today)
		{
			return false;
		}

		m_Changed = false;
		m_Prices = pricing.GetEntries();
		m_Folder = folder;
		m_Today = today;

		m_Sessions.clear();
		std::vector<const SessionSummary*> counted;

		for (const auto& [file, summary] : sessions)
		{
			SessionPlace place = PlaceOfSession(summary, pricing);

			if (!folder.empty() && !IsPlaceUnder(place, folder))
			{
				continue;
			}

			m_Sessions.push_back({.Summary = &summary, .Place = std::move(place)});

			if (!summary.IsEmpty())
			{
				counted.push_back(&summary);
			}
		}

		m_Model = BuildDashboard(counted, pricing, calendar, today);

		std::unordered_map<const SessionSummary*, std::size_t> indexOf;
		for (std::size_t index = 0; index < m_Model.Sessions.size(); ++index)
		{
			indexOf.emplace(m_Model.Sessions[index].Summary, index);
		}

		for (ListedSession& session : m_Sessions)
		{
			if (const auto found = indexOf.find(session.Summary); found != indexOf.end())
			{
				session.ModelIndex = found->second;
			}
		}

		return true;
	}

	std::string DashboardSource::Stamp() const
	{
		std::string stamp = "from " + TipCounted(m_Model.Sessions.size(), "transcript", "transcripts");

		const auto first = std::ranges::find_if(m_Model.Days, HasUsage);
		if (first == m_Model.Days.end())
		{
			return stamp;
		}

		const auto last = std::ranges::find_if(m_Model.Days.rbegin(), m_Model.Days.rend(), HasUsage);
		stamp += kMiddleDotSeparator;
		stamp += ShortDate(first->Day);

		if (last->Day != first->Day)
		{
			stamp += " ";
			stamp += kEnDash;
			stamp += " ";
			stamp += ShortDate(last->Day);
		}

		return stamp;
	}
}

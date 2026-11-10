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
		constexpr std::chrono::milliseconds kScanRebuildInterval{500};

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

	void DashboardSource::MarkChanged(SummaryChange change)
	{
		m_Changed = true;
		m_Removed = m_Removed || change == SummaryChange::Removed;
	}

	// WARNING: During a scan an update may wait for the interval, since the summaries live in a std::map: an updated
	// summary keeps its node, and the model's facts, which hold only a const SessionSummary*, keep pointing at it with
	// numbers a moment old. An erase frees the node such a pointer names, so a removal rebuilds at once.
	bool DashboardSource::IsDue(const RefreshMoment& moment) const
	{
		if (m_Removed || !m_LastBuild.has_value())
		{
			return true;
		}

		if (!m_Changed)
		{
			return false;
		}

		return !moment.Scanning || moment.Now - *m_LastBuild >= kScanRebuildInterval;
	}

	bool DashboardSource::Refresh(const SessionsByFile& sessions, const Pricing& pricing,
								  const std::filesystem::path& folder, const LocalCalendar& calendar,
								  std::chrono::year_month_day today, const RefreshMoment& moment)
	{
		const bool sameSettings = m_Prices == pricing.GetEntries() && m_Folder == folder && m_Today == today;

		if (sameSettings && !IsDue(moment))
		{
			return false;
		}

		m_Changed = false;
		m_Removed = false;
		m_LastBuild = moment.Now;
		m_Prices = pricing.GetEntries();
		m_Folder = folder;
		m_Today = today;

		m_Sessions.clear();
		std::vector<const SessionSummary*> counted;

		for (const auto& [file, summary] : sessions)
		{
			const bool counts = !summary.IsEmpty();
			const bool filtersByFolder = !folder.empty();
			const bool modelGivesPlace = counts && !filtersByFolder;
			SessionPlace place = modelGivesPlace ? SessionPlace{} : PlaceOfSession(summary, pricing);

			if (filtersByFolder && !IsPlaceUnder(place, folder))
			{
				continue;
			}

			m_Sessions.push_back({.Summary = &summary, .Place = std::move(place)});

			if (counts)
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
				session.Place = m_Model.Sessions[found->second].Place;
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

#pragma once

#include "Sessions/Overview/DashboardModel.h"
#include "Sessions/Overview/LocalCalendar.h"
#include "Sessions/Overview/SessionPlace.h"
#include "Sessions/Pricing.h"
#include "Sessions/SessionSummary.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	using SessionsByFile = std::map<std::filesystem::path, SessionSummary>;

	struct ListedSession
	{
		const SessionSummary* Summary = nullptr;
		SessionPlace Place;
		std::optional<std::size_t> ModelIndex; // into DashboardModel::Sessions; none for an empty transcript
	};

	// WARNING: A worktree's place is its own folder under the repository, so a folder open on a worktree keeps its
	// sessions and one open on the repository keeps its worktrees' too.
	bool IsPlaceUnder(const SessionPlace& place, const std::filesystem::path& folder);

	class DashboardSource
	{
	public:
		// WARNING: Call on every change to the summaries the source was refreshed from. The model and the sessions
		// point into them, and a change left unmarked leaves them pointing at a summary that has moved or gone.
		void MarkChanged();

		// WARNING: An empty folder means every folder, not none.
		bool Refresh(const SessionsByFile& sessions, const Pricing& pricing, const std::filesystem::path& folder,
					 const LocalCalendar& calendar, std::chrono::year_month_day today);

		const DashboardModel& GetModel() const { return m_Model; }

		std::span<const ListedSession> GetSessions() const { return m_Sessions; }

		std::string Stamp() const; // "from 12 transcripts · 7 Oct – 10 Nov"

	private:
		bool m_Changed = true;
		std::map<std::string, Price> m_Prices;
		std::filesystem::path m_Folder;
		std::chrono::year_month_day m_Today{};

		DashboardModel m_Model;
		std::vector<ListedSession> m_Sessions;
	};
}

#include "Notifications/NotificationHistory.h"

#include <utility>

namespace Graphite {

	namespace {
		bool IsConfirmation(Core::ToastKind kind)
		{
			return kind == Core::ToastKind::Success;
		}

		std::vector<NotificationEntry>::iterator FindByToastId(std::vector<NotificationEntry>& entries, std::uint64_t toastId)
		{
			for (std::vector<NotificationEntry>::iterator candidate = entries.begin(); candidate != entries.end(); ++candidate)
			{
				if (candidate->ToastId == toastId)
				{
					return candidate;
				}
			}

			return entries.end();
		}
	}

	void NotificationHistory::Record(NotificationEntry entry)
	{
		const std::vector<NotificationEntry>::iterator twin = FindByToastId(m_NewestFirst, entry.ToastId);
		if (twin != m_NewestFirst.end())
		{
			NotificationEntry merged = std::move(*twin);
			++merged.Count;
			merged.LastShownAt = entry.LastShownAt;
			m_NewestFirst.erase(twin);
			m_NewestFirst.insert(m_NewestFirst.begin(), std::move(merged));
			return;
		}

		m_NewestFirst.insert(m_NewestFirst.begin(), std::move(entry));

		const bool overTheLimit = m_NewestFirst.size() > kMostEntries;
		if (overTheLimit)
		{
			m_NewestFirst.pop_back();
		}
	}

	void NotificationHistory::NoteDeparture(const Core::ToastDeparture& departure)
	{
		const std::vector<NotificationEntry>::iterator entry = FindByToastId(m_NewestFirst, departure.Id);
		if (entry == m_NewestFirst.end())
		{
			return;
		}

		const bool leftOnItsOwn = departure.Reason != Core::ToastDepartureReason::ClosedByUser;
		const bool wentUnseen = leftOnItsOwn && !departure.WasHovered && !IsConfirmation(entry->Kind);
		if (wentUnseen)
		{
			entry->Missed = true;
		}
	}

	void NotificationHistory::MarkAllSeen()
	{
		for (NotificationEntry& entry : m_NewestFirst)
		{
			entry.Missed = false;
		}
	}

	void NotificationHistory::Remove(std::uint64_t toastId)
	{
		const std::vector<NotificationEntry>::iterator removed = FindByToastId(m_NewestFirst, toastId);
		if (removed == m_NewestFirst.end())
		{
			return;
		}

		m_NewestFirst.erase(removed);
	}

	void NotificationHistory::Clear()
	{
		m_NewestFirst.clear();
	}

	std::optional<Core::ToastKind> NotificationHistory::MostSevereMissed() const
	{
		bool anyWarningMissed = false;
		for (const NotificationEntry& entry : m_NewestFirst)
		{
			if (!entry.Missed)
			{
				continue;
			}

			if (entry.Kind == Core::ToastKind::Error)
			{
				return Core::ToastKind::Error;
			}

			if (entry.Kind == Core::ToastKind::Warning)
			{
				anyWarningMissed = true;
			}
		}

		if (anyWarningMissed)
		{
			return Core::ToastKind::Warning;
		}

		return std::nullopt;
	}

	std::vector<const NotificationEntry*> NotificationHistory::Entries(bool includeConfirmations) const
	{
		std::vector<const NotificationEntry*> shown;
		for (const NotificationEntry& entry : m_NewestFirst)
		{
			const bool filteredOut = IsConfirmation(entry.Kind) && !includeConfirmations;
			if (!filteredOut)
			{
				shown.push_back(&entry);
			}
		}

		return shown;
	}

	std::string CopiedText(const NotificationEntry& entry)
	{
		const bool hasOwnCopyText = !entry.CopyText.empty();
		return hasOwnCopyText ? entry.CopyText : entry.Title + "\n" + entry.Message;
	}
}

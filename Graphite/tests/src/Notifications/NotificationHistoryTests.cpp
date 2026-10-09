#include <doctest/doctest.h>

#include "Notifications/NotificationHistory.h"

#include <chrono>
#include <string>
#include <vector>

namespace {
	using namespace std::chrono_literals;

	const std::chrono::system_clock::time_point kMorning{std::chrono::sys_days{std::chrono::year{2026} / 10 / 6} + 9h};

	Graphite::NotificationEntry Entry(std::uint64_t toastId, Core::ToastKind kind, std::string title = "Title")
	{
		return {.ToastId = toastId, .Kind = kind, .Title = std::move(title), .Message = "message", .LastShownAt = kMorning};
	}

	Core::ToastDeparture Departure(std::uint64_t toastId, Core::ToastDepartureReason reason, bool wasHovered = false)
	{
		return {.Id = toastId, .Reason = reason, .WasHovered = wasHovered};
	}

	std::vector<std::uint64_t> IdsOf(const std::vector<const Graphite::NotificationEntry*>& entries)
	{
		std::vector<std::uint64_t> toastIds;
		for (const Graphite::NotificationEntry* entry : entries)
		{
			toastIds.push_back(entry->ToastId);
		}
		return toastIds;
	}
}

TEST_CASE("A recorded notification goes to the top of the history")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.Record(Entry(2, Core::ToastKind::Warning));

	CHECK(IdsOf(history.Entries(true)) == std::vector<std::uint64_t>{2, 1});
}

TEST_CASE("A notification for a toast already in the history counts it and moves it to the top")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.Record(Entry(2, Core::ToastKind::Warning));

	Graphite::NotificationEntry again = Entry(1, Core::ToastKind::Error);
	again.LastShownAt = kMorning + 5min;
	history.Record(again);

	const std::vector<const Graphite::NotificationEntry*> entries = history.Entries(true);
	REQUIRE(entries.size() == 2);
	CHECK(entries[0]->ToastId == 1);
	CHECK(entries[0]->Count == 2);
	CHECK(entries[0]->LastShownAt == kMorning + 5min);
}

TEST_CASE("A warning that expires unhovered is missed")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Warning));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::Expired));

	CHECK(history.Entries(true)[0]->Missed);
	CHECK(history.MostSevereMissed() == Core::ToastKind::Warning);
}

TEST_CASE("An error cleared with its folder is missed")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::Cleared));

	CHECK(history.MostSevereMissed() == Core::ToastKind::Error);
}

TEST_CASE("An error toast left to run out its twelve seconds unhovered is missed")
{
	Core::ToastStack stack;
	Graphite::NotificationHistory history;
	const Core::ToastStack::Clock::time_point shownAt{};
	const std::uint64_t toastId = stack.Show(Core::ToastKind::Error, "Push failed", "rejected", shownAt);
	history.Record(Entry(toastId, Core::ToastKind::Error));

	stack.Update(shownAt + 12s);
	for (const Core::ToastDeparture& departure : stack.TakeDepartures())
	{
		history.NoteDeparture(departure);
	}

	CHECK(history.Entries(true)[0]->Missed);
	CHECK(history.MostSevereMissed() == Core::ToastKind::Error);
}

TEST_CASE("A toast closed by the user is not missed")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::ClosedByUser));

	CHECK_FALSE(history.MostSevereMissed().has_value());
}

TEST_CASE("A toast the pointer rested on is not missed")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Warning));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::Expired, true));

	CHECK_FALSE(history.MostSevereMissed().has_value());
}

TEST_CASE("A confirmation is never missed")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Success));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::Expired));

	CHECK_FALSE(history.MostSevereMissed().has_value());
}

TEST_CASE("A departure for a toast no longer in the history is ignored")
{
	Graphite::NotificationHistory history;
	history.NoteDeparture(Departure(7, Core::ToastDepartureReason::Expired));

	CHECK(history.Entries(true).empty());
	CHECK_FALSE(history.MostSevereMissed().has_value());
}

TEST_CASE("A missed error outranks a missed warning whatever their order")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.Record(Entry(2, Core::ToastKind::Warning));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::Cleared));
	history.NoteDeparture(Departure(2, Core::ToastDepartureReason::Expired));

	CHECK(history.MostSevereMissed() == Core::ToastKind::Error);
}

TEST_CASE("Marking all seen leaves nothing missed")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.NoteDeparture(Departure(1, Core::ToastDepartureReason::Cleared));

	history.MarkAllSeen();

	CHECK_FALSE(history.MostSevereMissed().has_value());
	CHECK_FALSE(history.Entries(true)[0]->Missed);
}

TEST_CASE("Confirmations are left out of the entries unless asked for")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Success));
	history.Record(Entry(2, Core::ToastKind::Error));

	CHECK(IdsOf(history.Entries(false)) == std::vector<std::uint64_t>{2});
	CHECK(IdsOf(history.Entries(true)) == std::vector<std::uint64_t>{2, 1});
}

TEST_CASE("The history keeps only the newest two hundred notifications")
{
	Graphite::NotificationHistory history;
	const std::uint64_t oneMoreThanKept = Graphite::NotificationHistory::kMostEntries + 1;
	for (std::uint64_t toastId = 1; toastId <= oneMoreThanKept; ++toastId)
	{
		history.Record(Entry(toastId, Core::ToastKind::Warning));
	}

	const std::vector<const Graphite::NotificationEntry*> entries = history.Entries(true);
	REQUIRE(entries.size() == Graphite::NotificationHistory::kMostEntries);
	CHECK(entries.front()->ToastId == oneMoreThanKept);
	CHECK(entries.back()->ToastId == 2);
}

TEST_CASE("Clearing the history empties it")
{
	Graphite::NotificationHistory history;
	history.Record(Entry(1, Core::ToastKind::Error));
	history.Clear();

	CHECK(history.Entries(true).empty());
}

TEST_CASE("A notification copies its own copy text, or else its title and message one line each")
{
	Graphite::NotificationEntry plain = Entry(1, Core::ToastKind::Error, "Push failed");
	CHECK(Graphite::CopiedText(plain) == "Push failed\nmessage");

	plain.CopyText = "Push failed\nD:/Repos/IDE: rejected";
	CHECK(Graphite::CopiedText(plain) == "Push failed\nD:/Repos/IDE: rejected");
}

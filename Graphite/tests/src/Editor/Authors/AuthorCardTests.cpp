#include <doctest/doctest.h>

#include "Editor/Authors/AuthorCard.h"

#include "Git/Blame/HeadBlame.h"
#include "Git/Blame/LineAuthors.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"
#include "UI/TypographicMarks.h"

#include "Core/Git/Blame.h"
#include "Core/Git/Commit.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

using Graphite::AuthorCardInput;
using Graphite::AuthorCardTracker;

namespace {

	constexpr float kDelay = 0.5f;
	constexpr std::size_t kLine = 4;
	constexpr std::size_t kOtherLine = 7;
	constexpr std::uint8_t kCommitByte = 0xE5;
	constexpr std::chrono::sys_seconds kNow{std::chrono::seconds{1'800'000'000}};
	constexpr std::chrono::sys_seconds kCommittedAt = kNow - std::chrono::days{90};
	constexpr std::chrono::minutes kOffset{120};

	AuthorCardInput OverText(float seconds, std::size_t line = kLine)
	{
		return AuthorCardInput{.OverText = true, .Line = line, .Seconds = seconds};
	}

	AuthorCardInput OverCard(float seconds)
	{
		return AuthorCardInput{.OverCard = true, .Line = kLine, .Seconds = seconds};
	}

	AuthorCardInput Elsewhere(float seconds)
	{
		return AuthorCardInput{.Line = kLine, .Seconds = seconds};
	}

	AuthorCardInput ClickOnText()
	{
		return AuthorCardInput{.OverText = true, .ClickedText = true, .Line = kLine};
	}

	AuthorCardTracker ShownByAClick()
	{
		AuthorCardTracker tracker;
		tracker.Update(ClickOnText(), kDelay);
		REQUIRE(tracker.ShownLine() == kLine);

		return tracker;
	}

	Core::Oid CommitId()
	{
		Core::Oid commit;
		commit.Bytes.fill(kCommitByte);

		return commit;
	}

	// One committed line by Anna, then one uncommitted, then one with no author.
	Graphite::LineAuthors AuthorsOfThreeLines()
	{
		const Core::Oid commit = CommitId();

		auto blame = std::make_shared<Core::Blame>();
		blame->LineCount = 1;
		blame->Hunks = {Core::BlameHunk{.FirstLine = 1,
										.LineCount = 1,
										.Commit = commit,
										.OriginalPath = "src/Bus.cs",
										.OriginalFirstLine = 1}};

		Core::CommitSummary& summary = blame->Commits[commit];
		summary.Id = commit;
		summary.Author = Core::Signature{.Name = "Anna Kowalczyk", .Email = "anna.kowalczyk@studio.dev", .When = kCommittedAt};
		summary.Summary = "Iterate a copy so handlers can unsubscribe while publishing";
		summary.Body = "A handler that unsubscribed itself during Publish threw.";

		auto head = std::make_shared<Graphite::HeadBlame>();
		head->Tracked = true;
		head->Blame = std::move(blame);

		Graphite::LineAuthors authors;
		authors.Head = std::move(head);
		authors.Lines = {Graphite::LineAuthor{.Kind = Graphite::LineAuthorKind::Committed, .Hunk = 0, .HeadLine = 1},
						 Graphite::LineAuthor{.Kind = Graphite::LineAuthorKind::Uncommitted},
						 Graphite::LineAuthor{}};

		return authors;
	}
}

TEST_CASE("Author card waits for the hover delay")
{
	AuthorCardTracker tracker;

	tracker.Update(OverText(0.0f), kDelay);
	tracker.Update(OverText(0.3f), kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());
	REQUIRE(tracker.SecondsUntilShown().has_value());
	CHECK(*tracker.SecondsUntilShown() == doctest::Approx(0.2f));

	tracker.Update(OverText(0.25f), kDelay);

	CHECK(tracker.ShownLine() == kLine);
	CHECK_FALSE(tracker.SecondsUntilShown().has_value());
}

TEST_CASE("Author card does not count the time before the mouse reached the text")
{
	constexpr float kLongSleep = 5.0f;

	AuthorCardTracker tracker;

	tracker.Update(OverText(kLongSleep), kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());
	CHECK(tracker.SecondsUntilShown() == kDelay);
}

TEST_CASE("Author card opens at once on a click")
{
	AuthorCardTracker tracker;

	tracker.Update(ClickOnText(), kDelay);

	CHECK(tracker.ShownLine() == kLine);
	CHECK_FALSE(tracker.SecondsUntilShown().has_value());
}

TEST_CASE("Author card stays open while the mouse is in it")
{
	AuthorCardTracker tracker = ShownByAClick();

	tracker.Update(Elsewhere(0.0f), kDelay);
	tracker.Update(Elsewhere(0.1f), kDelay);
	tracker.Update(OverCard(0.1f), kDelay);
	tracker.Update(OverCard(10.0f), kDelay);

	CHECK(tracker.ShownLine() == kLine);
	CHECK_FALSE(tracker.SecondsUntilHidden().has_value());
}

TEST_CASE("Author card closes after the grace once the mouse has left")
{
	AuthorCardTracker tracker = ShownByAClick();

	tracker.Update(Elsewhere(0.0f), kDelay);
	tracker.Update(Elsewhere(0.1f), kDelay);

	CHECK(tracker.ShownLine() == kLine);
	REQUIRE(tracker.SecondsUntilHidden().has_value());
	CHECK(*tracker.SecondsUntilHidden() == doctest::Approx(Graphite::kAuthorCardLeaveGraceSeconds - 0.1f));

	tracker.Update(Elsewhere(0.2f), kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());
	CHECK_FALSE(tracker.SecondsUntilHidden().has_value());
}

TEST_CASE("Author card closes on a dismissal and waits for the mouse to leave")
{
	AuthorCardTracker tracker = ShownByAClick();

	AuthorCardInput key = OverText(0.0f);
	key.Dismissed = true;
	tracker.Update(key, kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());

	tracker.Update(OverText(1.0f), kDelay);
	tracker.Update(OverText(1.0f), kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());
	CHECK_FALSE(tracker.SecondsUntilShown().has_value());

	tracker.Update(Elsewhere(0.1f), kDelay);
	tracker.Update(OverText(0.0f), kDelay);
	tracker.Update(OverText(kDelay), kDelay);

	CHECK(tracker.ShownLine() == kLine);
}

TEST_CASE("Author card waits for the mouse to leave a text hidden while typing")
{
	AuthorCardTracker tracker = ShownByAClick();

	AuthorCardInput key = OverText(0.0f);
	key.Dismissed = true;
	tracker.Update(key, kDelay);

	AuthorCardInput hidden = Elsewhere(1.0f);
	hidden.TextShown = false;
	tracker.Update(hidden, kDelay);

	tracker.Update(OverText(0.0f), kDelay);
	tracker.Update(OverText(1.0f), kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());
}

TEST_CASE("Author card restarts the delay on another line")
{
	AuthorCardTracker tracker = ShownByAClick();

	tracker.Update(OverText(0.0f, kOtherLine), kDelay);

	CHECK_FALSE(tracker.ShownLine().has_value());
	CHECK(tracker.SecondsUntilShown() == kDelay);

	tracker.Update(OverText(kDelay, kOtherLine), kDelay);

	CHECK(tracker.ShownLine() == kOtherLine);
}

TEST_CASE("Author card content names the commit")
{
	const Graphite::LineAuthors authors = AuthorsOfThreeLines();

	const std::optional<Graphite::AuthorCardContent> content = Graphite::BuildAuthorCard(authors, 0, kNow, kOffset);

	REQUIRE(content.has_value());
	CHECK(content->Name == "Anna Kowalczyk");
	CHECK(content->Email == "anna.kowalczyk@studio.dev");
	CHECK(content->When == Graphite::RelativeAge(kCommittedAt, kNow) + std::string{Graphite::kMiddleDotSeparator} +
							   Graphite::FormattedTimestamp(kCommittedAt, kOffset));
	CHECK(content->Title == "Iterate a copy so handlers can unsubscribe while publishing");
	CHECK(content->Body == "A handler that unsubscribed itself during Publish threw.");
	CHECK(content->ShortHash == "e5e5e5e");
	CHECK(content->FullHash == CommitId().ToHex());
	CHECK_FALSE(content->Uncommitted);
}

TEST_CASE("Author card content for an uncommitted line says You")
{
	const Graphite::LineAuthors authors = AuthorsOfThreeLines();

	const std::optional<Graphite::AuthorCardContent> content = Graphite::BuildAuthorCard(authors, 1, kNow, kOffset);

	REQUIRE(content.has_value());
	CHECK(content->Name == "You");
	CHECK(content->Title == "Uncommitted changes");
	CHECK(content->Body == "These changes are not committed yet.");
	CHECK(content->ShortHash.empty());
	CHECK(content->FullHash.empty());
	CHECK(content->Uncommitted);
}

TEST_CASE("Author card content is empty for a line with no author or past the table")
{
	const Graphite::LineAuthors authors = AuthorsOfThreeLines();

	CHECK_FALSE(Graphite::BuildAuthorCard(authors, 2, kNow, kOffset).has_value());
	CHECK_FALSE(Graphite::BuildAuthorCard(authors, 3, kNow, kOffset).has_value());
}

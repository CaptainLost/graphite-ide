#include "corepch.h"

#include "Core/FileSystem/ChangeCollector.h"

#include <doctest/doctest.h>

using namespace Core;

namespace {
	// A fixed start rather than the real clock, so that these tests do not depend on how fast
	// they run.
	constexpr ChangeCollector::Clock::time_point kFixedStart{};

	ChangeCollector::Clock::time_point AtMillisecond(int milliseconds)
	{
		return kFixedStart + std::chrono::milliseconds{milliseconds};
	}

	constexpr std::chrono::milliseconds kQuietPeriod{250};
}

TEST_CASE("An empty collector hands over nothing")
{
	ChangeCollector collector;

	CHECK(collector.TakeSettled(AtMillisecond(1000), kQuietPeriod).empty());
}

TEST_CASE("A change is withheld until the quiet period has passed")
{
	ChangeCollector collector;
	collector.Add("C:/work/src", AtMillisecond(0));

	CHECK(collector.TakeSettled(AtMillisecond(100), kQuietPeriod).empty());
	CHECK(collector.TakeSettled(AtMillisecond(249), kQuietPeriod).empty());

	const std::vector<std::filesystem::path> settled = collector.TakeSettled(AtMillisecond(250), kQuietPeriod);

	REQUIRE(settled.size() == 1);
	CHECK(settled[0] == std::filesystem::path{"C:/work/src"});
}

TEST_CASE("A later change restarts the quiet period so that a build still writing is not handed over half-written")
{
	ChangeCollector collector;
	collector.Add("C:/work/src", AtMillisecond(0));

	const ChangeCollector::Clock::time_point beforeTheFirstSettled = AtMillisecond(200);
	collector.Add("C:/work/docs", beforeTheFirstSettled);

	CHECK(collector.TakeSettled(AtMillisecond(300), kQuietPeriod).empty());
	CHECK(collector.TakeSettled(AtMillisecond(450), kQuietPeriod).size() == 2);
}

TEST_CASE("The same directory is reported once however often it changed")
{
	ChangeCollector collector;
	collector.Add("C:/work/src", AtMillisecond(0));
	collector.Add("C:/work/src", AtMillisecond(10));
	collector.Add("C:/work/src", AtMillisecond(20));

	CHECK(collector.TakeSettled(AtMillisecond(500), kQuietPeriod).size() == 1);
}

TEST_CASE("Taking settled changes empties the collector")
{
	ChangeCollector collector;
	collector.Add("C:/work/src", AtMillisecond(0));

	REQUIRE(collector.TakeSettled(AtMillisecond(500), kQuietPeriod).size() == 1);
	CHECK(collector.TakeSettled(AtMillisecond(1000), kQuietPeriod).empty());
}

TEST_CASE("Overflow is not reported until it settles and is then reported by one take only so the consumer acts on it once")
{
	ChangeCollector collector;
	collector.AddOverflow(AtMillisecond(0));

	CHECK_FALSE(collector.HasOverflowed());

	REQUIRE(collector.TakeSettled(AtMillisecond(500), kQuietPeriod).empty());
	CHECK(collector.HasOverflowed());

	REQUIRE(collector.TakeSettled(AtMillisecond(1000), kQuietPeriod).empty());
	CHECK_FALSE(collector.HasOverflowed());
}

TEST_CASE("Overflow settles alongside ordinary changes")
{
	ChangeCollector collector;
	collector.Add("C:/work/src", AtMillisecond(0));
	collector.AddOverflow(AtMillisecond(100));

	CHECK(collector.TakeSettled(AtMillisecond(200), kQuietPeriod).empty());

	const std::vector<std::filesystem::path> settled = collector.TakeSettled(AtMillisecond(400), kQuietPeriod);

	CHECK(settled.size() == 1);
	CHECK(collector.HasOverflowed());
}

TEST_CASE("Nothing pending settles at no time")
{
	ChangeCollector collector;

	CHECK_FALSE(collector.SettlesAt(kQuietPeriod).has_value());
}

TEST_CASE("Pending changes settle a quiet period after the last one which is the earliest a take hands them over")
{
	ChangeCollector collector;
	collector.Add("C:/a", AtMillisecond(0));
	collector.Add("C:/b", AtMillisecond(100));

	REQUIRE(collector.SettlesAt(kQuietPeriod).has_value());
	CHECK(*collector.SettlesAt(kQuietPeriod) == AtMillisecond(350));

	CHECK(collector.TakeSettled(AtMillisecond(349), kQuietPeriod).empty());
	CHECK(collector.TakeSettled(AtMillisecond(350), kQuietPeriod).size() == 2);
}

TEST_CASE("Settled changes taken leave nothing to settle")
{
	ChangeCollector collector;
	collector.Add("C:/a", AtMillisecond(0));
	(void)collector.TakeSettled(AtMillisecond(300), kQuietPeriod);

	CHECK_FALSE(collector.SettlesAt(kQuietPeriod).has_value());
}

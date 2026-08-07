#include "corepch.h"

#include "Core/Fs/ChangeCollector.h"

#include <doctest/doctest.h>

using namespace core;

namespace
{
    // A fixed origin plus explicit offsets: the rule under test is about elapsed time,
    // and reading the real clock would make these tests depend on how fast they run.
    constexpr ChangeCollector::Clock::time_point kStart{};

    ChangeCollector::Clock::time_point At(int milliseconds)
    {
        return kStart + std::chrono::milliseconds{milliseconds};
    }

    constexpr std::chrono::milliseconds kQuiet{250};
}

TEST_CASE("An empty collector hands over nothing")
{
    ChangeCollector collector;

    CHECK(collector.TakeSettled(At(1000), kQuiet).empty());
}

TEST_CASE("A change is withheld until the quiet period has passed")
{
    ChangeCollector collector;
    collector.Add("C:/work/src", At(0));

    CHECK(collector.TakeSettled(At(100), kQuiet).empty());
    CHECK(collector.TakeSettled(At(249), kQuiet).empty());

    const auto settled = collector.TakeSettled(At(250), kQuiet);

    REQUIRE(settled.size() == 1);
    CHECK(settled[0] == std::filesystem::path{"C:/work/src"});
}

TEST_CASE("A later change restarts the quiet period")
{
    ChangeCollector collector;
    collector.Add("C:/work/src", At(0));

    // The second change arrives before the first has settled: a build writes for
    // several seconds, and refreshing partway through would show a half-written state.
    collector.Add("C:/work/docs", At(200));

    CHECK(collector.TakeSettled(At(300), kQuiet).empty());
    CHECK(collector.TakeSettled(At(450), kQuiet).size() == 2);
}

TEST_CASE("The same directory is reported once however often it changed")
{
    ChangeCollector collector;
    collector.Add("C:/work/src", At(0));
    collector.Add("C:/work/src", At(10));
    collector.Add("C:/work/src", At(20));

    CHECK(collector.TakeSettled(At(500), kQuiet).size() == 1);
}

TEST_CASE("Taking settled changes empties the collector")
{
    ChangeCollector collector;
    collector.Add("C:/work/src", At(0));

    REQUIRE(collector.TakeSettled(At(500), kQuiet).size() == 1);
    CHECK(collector.TakeSettled(At(1000), kQuiet).empty());
}

TEST_CASE("Overflow is reported once and then cleared")
{
    ChangeCollector collector;
    collector.AddOverflow(At(0));

    CHECK_FALSE(collector.Overflowed());   // not until it settles

    REQUIRE(collector.TakeSettled(At(500), kQuiet).empty());
    CHECK(collector.Overflowed());

    // The consumer has acted on it; a second read must not make it act again.
    REQUIRE(collector.TakeSettled(At(1000), kQuiet).empty());
    CHECK_FALSE(collector.Overflowed());
}

TEST_CASE("Overflow settles alongside ordinary changes")
{
    ChangeCollector collector;
    collector.Add("C:/work/src", At(0));
    collector.AddOverflow(At(100));

    CHECK(collector.TakeSettled(At(200), kQuiet).empty());

    const auto settled = collector.TakeSettled(At(400), kQuiet);

    CHECK(settled.size() == 1);
    CHECK(collector.Overflowed());
}

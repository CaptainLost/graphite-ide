#include <doctest/doctest.h>

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"

#include <memory>

using Core::LogEntry;
using Core::Logger;
using Core::LogLevel;
using Core::RingBufferSink;

TEST_CASE("RingBufferSink remembers entries in order")
{
    RingBufferSink sink{4};

    sink.Write(LogEntry{LogLevel::Info, "first"});
    sink.Write(LogEntry{LogLevel::Warn, "second"});

    const auto entries = sink.Snapshot();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].message == "first");
    CHECK(entries[1].level == LogLevel::Warn);
}

TEST_CASE("RingBufferSink drops the oldest entries on overflow")
{
    RingBufferSink sink{3};

    for (int i = 1; i <= 5; ++i)
        sink.Write(LogEntry{LogLevel::Info, std::to_string(i)});

    const auto entries = sink.Snapshot();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].message == "3");
    CHECK(entries[1].message == "4");
    CHECK(entries[2].message == "5");
}

TEST_CASE("Clear empties the buffer")
{
    RingBufferSink sink{4};
    sink.Write(LogEntry{LogLevel::Info, "whatever"});

    sink.Clear();

    CHECK(sink.Count() == 0);
    CHECK(sink.Snapshot().empty());
}

TEST_CASE("Logger dispatches entries to attached sinks")
{
    auto sink = std::make_shared<RingBufferSink>(8);

    Logger::Get().ClearSinks();
    Logger::Get().SetLevel(LogLevel::Trace);
    Logger::Get().AddSink(sink);
    Logger::Get().Write(LogLevel::Error, "failure");

    const auto entries = sink->Snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].level == LogLevel::Error);
    CHECK(entries[0].message == "failure");

    Logger::Get().ClearSinks();
}

TEST_CASE("Logger filters out entries below the configured level")
{
    auto sink = std::make_shared<RingBufferSink>(8);

    Logger::Get().ClearSinks();
    Logger::Get().AddSink(sink);
    Logger::Get().SetLevel(LogLevel::Warn);

    Logger::Get().Write(LogLevel::Info, "skipped");
    Logger::Get().Write(LogLevel::Error, "passed through");

    const auto entries = sink->Snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].message == "passed through");

    Logger::Get().ClearSinks();
    Logger::Get().SetLevel(LogLevel::Trace);
}

TEST_CASE("The CORE_INFO macro formats arguments")
{
    auto sink = std::make_shared<RingBufferSink>(8);

    Logger::Get().ClearSinks();
    Logger::Get().SetLevel(LogLevel::Trace);
    Logger::Get().AddSink(sink);

    CORE_INFO("file {} has {} lines", "main.cpp", 12);

    const auto entries = sink->Snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].message == "file main.cpp has 12 lines");

    Logger::Get().ClearSinks();
}

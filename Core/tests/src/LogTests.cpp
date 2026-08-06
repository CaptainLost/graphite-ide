#include <doctest/doctest.h>

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"

#include <memory>

using core::LogEntry;
using core::Logger;
using core::LogLevel;
using core::RingBufferSink;

TEST_CASE("RingBufferSink zapamietuje wpisy w kolejnosci")
{
    RingBufferSink sink{4};

    sink.Write(LogEntry{LogLevel::Info, "pierwszy"});
    sink.Write(LogEntry{LogLevel::Warn, "drugi"});

    const auto entries = sink.Snapshot();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].message == "pierwszy");
    CHECK(entries[1].level == LogLevel::Warn);
}

TEST_CASE("RingBufferSink po przepelnieniu gubi najstarsze wpisy")
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

TEST_CASE("Clear opróżnia bufor")
{
    RingBufferSink sink{4};
    sink.Write(LogEntry{LogLevel::Info, "cokolwiek"});

    sink.Clear();

    CHECK(sink.Count() == 0);
    CHECK(sink.Snapshot().empty());
}

TEST_CASE("Logger rozsyla wpisy do podpietych sinkow")
{
    auto sink = std::make_shared<RingBufferSink>(8);

    Logger::Get().ClearSinks();
    Logger::Get().SetLevel(LogLevel::Trace);
    Logger::Get().AddSink(sink);
    Logger::Get().Write(LogLevel::Error, "awaria");

    const auto entries = sink->Snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].level == LogLevel::Error);
    CHECK(entries[0].message == "awaria");

    Logger::Get().ClearSinks();
}

TEST_CASE("Logger odfiltrowuje wpisy ponizej ustawionego poziomu")
{
    auto sink = std::make_shared<RingBufferSink>(8);

    Logger::Get().ClearSinks();
    Logger::Get().AddSink(sink);
    Logger::Get().SetLevel(LogLevel::Warn);

    Logger::Get().Write(LogLevel::Info, "pominiety");
    Logger::Get().Write(LogLevel::Error, "przepuszczony");

    const auto entries = sink->Snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].message == "przepuszczony");

    Logger::Get().ClearSinks();
    Logger::Get().SetLevel(LogLevel::Trace);
}

TEST_CASE("Makro CORE_INFO formatuje argumenty")
{
    auto sink = std::make_shared<RingBufferSink>(8);

    Logger::Get().ClearSinks();
    Logger::Get().SetLevel(LogLevel::Trace);
    Logger::Get().AddSink(sink);

    CORE_INFO("plik {} ma {} linii", "main.cpp", 12);

    const auto entries = sink->Snapshot();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].message == "plik main.cpp ma 12 linii");

    Logger::Get().ClearSinks();
}

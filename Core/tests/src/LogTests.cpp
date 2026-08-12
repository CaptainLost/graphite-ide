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

	sink.Write(LogEntry{LogLevel::Info, Core::LogSource::Engine, "first"});
	sink.Write(LogEntry{LogLevel::Warn, Core::LogSource::Engine, "second"});

	const auto entries = sink.TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message == "first");
	CHECK(entries[1].Level == LogLevel::Warn);
}

TEST_CASE("RingBufferSink drops the oldest entries on overflow")
{
	RingBufferSink sink{3};

	for (int i = 1; i <= 5; ++i)
		sink.Write(LogEntry{LogLevel::Info, Core::LogSource::Engine, std::to_string(i)});

	const auto entries = sink.TakeSnapshot();
	REQUIRE(entries.size() == 3);
	CHECK(entries[0].Message == "3");
	CHECK(entries[1].Message == "4");
	CHECK(entries[2].Message == "5");
}

TEST_CASE("Clear empties the buffer")
{
	RingBufferSink sink{4};
	sink.Write(LogEntry{LogLevel::Info, Core::LogSource::Engine, "whatever"});

	sink.Clear();

	CHECK(sink.GetCount() == 0);
	CHECK(sink.TakeSnapshot().empty());
}

TEST_CASE("Logger dispatches entries to attached sinks")
{
	auto sink = std::make_shared<RingBufferSink>(8);

	Logger::Get().ClearSinks();
	Logger::Get().SetLevel(LogLevel::Trace);
	Logger::Get().AddSink(sink);
	Logger::Get().Write(Core::LogSource::Engine, LogLevel::Error, "failure");

	const auto entries = sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Level == LogLevel::Error);
	CHECK(entries[0].Message == "failure");

	Logger::Get().ClearSinks();
}

TEST_CASE("Logger filters out entries below the configured level")
{
	auto sink = std::make_shared<RingBufferSink>(8);

	Logger::Get().ClearSinks();
	Logger::Get().AddSink(sink);
	Logger::Get().SetLevel(LogLevel::Warn);

	Logger::Get().Write(Core::LogSource::Engine, LogLevel::Info, "skipped");
	Logger::Get().Write(Core::LogSource::Engine, LogLevel::Error, "passed through");

	const auto entries = sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "passed through");

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

	const auto entries = sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "file main.cpp has 12 lines");

	Logger::Get().ClearSinks();
}

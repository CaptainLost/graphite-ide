#include <doctest/doctest.h>

#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"

#include "TempDirectory.h"

#include <spdlog/logger.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using Core::Log;
using Core::LogLevel;
using Core::LogSource;
using Core::RingBufferSink;

namespace {
	// A logger and a buffer of its own, so the buffer's behaviour can be tested without
	// the two program-wide loggers and whatever else has already written through them.
	struct PrivateLog
	{
		explicit PrivateLog(std::size_t capacity, LogSource source = LogSource::Engine)
			: Sink(std::make_shared<RingBufferSink>(capacity)),
			  Logger(std::string{Core::ToString(source)}, Sink)
		{
			Logger.set_level(spdlog::level::trace);
		}

		Core::Ref<RingBufferSink> Sink;
		spdlog::logger Logger;
	};

	std::string ReadFile(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}
}

TEST_CASE("RingBufferSink remembers entries in order")
{
	PrivateLog log{4};

	log.Logger.info("first");
	log.Logger.warn("second");

	const auto entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message == "first");
	CHECK(entries[0].Level == LogLevel::Info);
	CHECK(entries[1].Level == LogLevel::Warn);
}

TEST_CASE("RingBufferSink drops the oldest entries on overflow")
{
	PrivateLog log{3};

	for (int i = 1; i <= 5; ++i)
		log.Logger.info("{}", i);

	const auto entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 3);
	CHECK(log.Sink->GetCount() == 3);
	CHECK(log.Sink->GetCapacity() == 3);
	CHECK(entries[0].Message == "3");
	CHECK(entries[1].Message == "4");
	CHECK(entries[2].Message == "5");
}

TEST_CASE("Clear empties the buffer")
{
	PrivateLog log{4};
	log.Logger.info("whatever");

	log.Sink->Clear();

	CHECK(log.Sink->GetCount() == 0);
	CHECK(log.Sink->TakeSnapshot().empty());
}

// What lets a reader skip copying the buffer on a frame where nothing in it changed.
TEST_CASE("The buffer's revision moves with every entry and every clear, and with nothing else")
{
	PrivateLog log{2};
	const std::uint64_t start = log.Sink->GetRevision();

	(void)log.Sink->TakeSnapshot();
	CHECK(log.Sink->GetRevision() == start);

	log.Logger.info("a");
	const std::uint64_t afterEntry = log.Sink->GetRevision();
	CHECK(afterEntry != start);

	log.Sink->Clear();
	CHECK(log.Sink->GetRevision() != afterEntry);
}

TEST_CASE("The buffer keeps every level apart")
{
	PrivateLog log{8};

	log.Logger.trace("t");
	log.Logger.info("i");
	log.Logger.warn("w");
	log.Logger.error("e");
	log.Logger.critical("c");

	const auto entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 5);
	CHECK(entries[0].Level == LogLevel::Trace);
	CHECK(entries[1].Level == LogLevel::Info);
	CHECK(entries[2].Level == LogLevel::Warn);
	CHECK(entries[3].Level == LogLevel::Error);
	CHECK(entries[4].Level == LogLevel::Critical);
}

TEST_CASE("Entries below the logger's level never reach the buffer")
{
	PrivateLog log{8};
	log.Logger.set_level(Core::ToBackendLevel(LogLevel::Warn));

	log.Logger.info("skipped");
	log.Logger.error("passed through");

	const auto entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "passed through");
}

TEST_CASE("An entry remembers which half of the program wrote it")
{
	PrivateLog engine{4, LogSource::Engine};
	PrivateLog application{4, LogSource::Application};

	engine.Logger.info("from the engine");
	application.Logger.info("from the application");

	const auto engineEntries = engine.Sink->TakeSnapshot();
	const auto applicationEntries = application.Sink->TakeSnapshot();
	REQUIRE(engineEntries.size() == 1);
	REQUIRE(applicationEntries.size() == 1);
	CHECK(engineEntries[0].Source == LogSource::Engine);
	CHECK(applicationEntries[0].Source == LogSource::Application);
}

TEST_CASE("The CORE_INFO macro formats arguments into the console buffer")
{
	Log::GetConsoleSink()->Clear();

	CORE_INFO("file {} has {} lines", "main.cpp", 12);

	const auto entries = Log::GetConsoleSink()->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "file main.cpp has 12 lines");
	CHECK(entries[0].Source == LogSource::Engine);
	CHECK(entries[0].Level == LogLevel::Info);

	Log::GetConsoleSink()->Clear();
}

TEST_CASE("The APP_ macros write as the application")
{
	Log::GetConsoleSink()->Clear();

	APP_WARN("{} of {} files failed", 2, 3);

	const auto entries = Log::GetConsoleSink()->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "2 of 3 files failed");
	CHECK(entries[0].Source == LogSource::Application);
	CHECK(entries[0].Level == LogLevel::Warn);

	Log::GetConsoleSink()->Clear();
}

TEST_CASE("Init writes to the file it was given and keeps the buffer it already filled")
{
	const TempDirectory temp;

	Log::GetConsoleSink()->Clear();
	CORE_INFO("written before Init");

	Core::LogSpec spec;
	spec.FilePath = temp.GetPath() / "nested" / "core.log";
	Log::Init(spec);

	CORE_ERROR("written to the file");

	// Dropping the file sink closes the file, so it can be read here and removed with the
	// directory afterwards.
	Log::Init(Core::LogSpec{});

	const auto entries = Log::GetConsoleSink()->TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message == "written before Init");

	const std::string contents = ReadFile(spec.FilePath);
	CHECK(contents.find("written to the file") != std::string::npos);
	CHECK(contents.find("written before Init") == std::string::npos);

	Log::GetConsoleSink()->Clear();
}

TEST_CASE("A capacity change replaces the console buffer")
{
	Log::GetConsoleSink()->Clear();
	CORE_INFO("written before the resize");

	Core::LogSpec spec;
	spec.ConsoleCapacity = 16;
	Log::Init(spec);

	CHECK(Log::GetConsoleSink()->GetCapacity() == 16);
	CHECK(Log::GetConsoleSink()->GetCount() == 0);

	Log::Init(Core::LogSpec{});
	CHECK(Log::GetConsoleSink()->GetCapacity() == Core::LogSpec{}.ConsoleCapacity);
}

TEST_CASE("RingBufferSink numbers entries so a reader can name one after older ones are dropped")
{
	PrivateLog log{2};

	log.Logger.info("first");
	log.Logger.info("second");
	log.Logger.info("third");

	const auto entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message == "second");
	CHECK(entries[1].Sequence == entries[0].Sequence + 1);
	// The numbers name a message rather than a slot: nothing left in the buffer wears the
	// number the dropped message had.
	CHECK(entries[0].Sequence > 0);
}

TEST_CASE("Clearing the buffer does not hand a later message a number already used")
{
	PrivateLog log{4};

	log.Logger.info("before");
	const std::uint64_t before = log.Sink->TakeSnapshot().front().Sequence;

	log.Sink->Clear();
	log.Logger.info("after");

	CHECK(log.Sink->TakeSnapshot().front().Sequence > before);
}

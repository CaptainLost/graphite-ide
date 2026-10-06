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
#include <vector>

using Core::Log;
using Core::LogEntry;
using Core::LogLevel;
using Core::LogSource;
using Core::RingBufferSink;

namespace {
	struct LoggerWithOwnSink
	{
		explicit LoggerWithOwnSink(std::size_t capacity, LogSource source = LogSource::Engine)
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

	void CloseLogFileSoTheTemporaryDirectoryCanBeRemoved()
	{
		Log::Init(Core::LogSpec{});
	}
}

TEST_CASE("RingBufferSink remembers entries in order")
{
	LoggerWithOwnSink log{4};

	log.Logger.info("first");
	log.Logger.warn("second");

	const std::vector<LogEntry> entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message == "first");
	CHECK(entries[0].Level == LogLevel::Info);
	CHECK(entries[1].Level == LogLevel::Warn);
}

TEST_CASE("RingBufferSink drops the oldest entries on overflow")
{
	LoggerWithOwnSink log{3};

	for (int messageNumber = 1; messageNumber <= 5; ++messageNumber)
	{
		log.Logger.info("{}", messageNumber);
	}

	const std::vector<LogEntry> entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 3);
	CHECK(log.Sink->GetCount() == 3);
	CHECK(log.Sink->GetCapacity() == 3);
	CHECK(entries[0].Message == "3");
	CHECK(entries[1].Message == "4");
	CHECK(entries[2].Message == "5");
}

TEST_CASE("Clear empties the buffer")
{
	LoggerWithOwnSink log{4};
	log.Logger.info("whatever");

	log.Sink->Clear();

	CHECK(log.Sink->GetCount() == 0);
	CHECK(log.Sink->TakeSnapshot().empty());
}

TEST_CASE("A reader can skip copying the buffer because its revision moves with every entry and every clear and with nothing else")
{
	LoggerWithOwnSink log{2};
	const std::uint64_t revisionAtStart = log.Sink->GetRevision();

	(void)log.Sink->TakeSnapshot();
	CHECK(log.Sink->GetRevision() == revisionAtStart);

	log.Logger.info("a");
	const std::uint64_t revisionAfterEntry = log.Sink->GetRevision();
	CHECK(revisionAfterEntry != revisionAtStart);

	log.Sink->Clear();
	CHECK(log.Sink->GetRevision() != revisionAfterEntry);
}

TEST_CASE("The buffer keeps every level apart")
{
	LoggerWithOwnSink log{8};

	log.Logger.trace("t");
	log.Logger.info("i");
	log.Logger.warn("w");
	log.Logger.error("e");
	log.Logger.critical("c");

	const std::vector<LogEntry> entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 5);
	CHECK(entries[0].Level == LogLevel::Trace);
	CHECK(entries[1].Level == LogLevel::Info);
	CHECK(entries[2].Level == LogLevel::Warn);
	CHECK(entries[3].Level == LogLevel::Error);
	CHECK(entries[4].Level == LogLevel::Critical);
}

TEST_CASE("Entries below the logger's level never reach the buffer")
{
	LoggerWithOwnSink log{8};
	log.Logger.set_level(Core::ToBackendLevel(LogLevel::Warn));

	log.Logger.info("skipped");
	log.Logger.error("passed through");

	const std::vector<LogEntry> entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "passed through");
}

TEST_CASE("An entry remembers which half of the program wrote it")
{
	LoggerWithOwnSink engine{4, LogSource::Engine};
	LoggerWithOwnSink application{4, LogSource::Application};

	engine.Logger.info("from the engine");
	application.Logger.info("from the application");

	const std::vector<LogEntry> engineEntries = engine.Sink->TakeSnapshot();
	const std::vector<LogEntry> applicationEntries = application.Sink->TakeSnapshot();
	REQUIRE(engineEntries.size() == 1);
	REQUIRE(applicationEntries.size() == 1);
	CHECK(engineEntries[0].Source == LogSource::Engine);
	CHECK(applicationEntries[0].Source == LogSource::Application);
}

TEST_CASE("The CORE_INFO macro formats arguments into the console buffer")
{
	Log::GetConsoleSink()->Clear();

	CORE_INFO("file {} has {} lines", "main.cpp", 12);

	const std::vector<LogEntry> entries = Log::GetConsoleSink()->TakeSnapshot();
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

	const std::vector<LogEntry> entries = Log::GetConsoleSink()->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "2 of 3 files failed");
	CHECK(entries[0].Source == LogSource::Application);
	CHECK(entries[0].Level == LogLevel::Warn);

	Log::GetConsoleSink()->Clear();
}

TEST_CASE("Init writes to the file it was given and keeps the buffer it already filled")
{
	const TempDirectory temporaryDirectory;

	Log::GetConsoleSink()->Clear();
	CORE_INFO("written before Init");

	const Core::LogSpec spec{.FilePath = temporaryDirectory.GetPath() / "nested" / "core.log"};
	Log::Init(spec);

	CORE_ERROR("written to the file");

	CloseLogFileSoTheTemporaryDirectoryCanBeRemoved();

	const std::vector<LogEntry> entries = Log::GetConsoleSink()->TakeSnapshot();
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

	const Core::LogSpec spec{.ConsoleCapacity = 16};
	Log::Init(spec);

	CHECK(Log::GetConsoleSink()->GetCapacity() == spec.ConsoleCapacity);
	CHECK(Log::GetConsoleSink()->GetCount() == 0);

	const Core::LogSpec defaultSpec{};
	Log::Init(defaultSpec);
	CHECK(Log::GetConsoleSink()->GetCapacity() == defaultSpec.ConsoleCapacity);
}

TEST_CASE("RingBufferSink numbers entries so a reader can name one after older ones are dropped")
{
	LoggerWithOwnSink log{2};

	log.Logger.info("first");
	const std::uint64_t sequenceOfDroppedMessage = log.Sink->TakeSnapshot().front().Sequence;
	log.Logger.info("second");
	log.Logger.info("third");

	const std::vector<LogEntry> entries = log.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message == "second");
	CHECK(entries[1].Sequence == entries[0].Sequence + 1);
	CHECK(entries[0].Sequence != sequenceOfDroppedMessage);
	CHECK(entries[1].Sequence != sequenceOfDroppedMessage);
	CHECK(entries[0].Sequence > 0);
}

TEST_CASE("Clearing the buffer does not hand a later message a number already used")
{
	LoggerWithOwnSink log{4};

	log.Logger.info("before");
	const std::uint64_t sequenceBeforeClear = log.Sink->TakeSnapshot().front().Sequence;

	log.Sink->Clear();
	log.Logger.info("after");

	CHECK(log.Sink->TakeSnapshot().front().Sequence > sequenceBeforeClear);
}

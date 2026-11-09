#include <doctest/doctest.h>

#include "Session/AgentLogging.h"

#include "Core/Base/Log.h"

#include <spdlog/sinks/ansicolor_sink.h>

#include <memory>

// The agent's stdout carries frames and nothing else: one log line there and Graphite reads
// garbage. Every sink of both loggers must write to stderr.
TEST_CASE("The agent's logs go to stderr, never to the stdout its frames use")
{
	Agent::SendLogsToStandardError();

	for (const Core::Ref<spdlog::logger>& logger : {Core::Log::GetEngineLogger(), Core::Log::GetApplicationLogger()})
	{
		REQUIRE_FALSE(logger->sinks().empty());

		for (const spdlog::sink_ptr& sink : logger->sinks())
		{
			CHECK(std::dynamic_pointer_cast<spdlog::sinks::ansicolor_stderr_sink_mt>(sink) != nullptr);
		}
	}
}

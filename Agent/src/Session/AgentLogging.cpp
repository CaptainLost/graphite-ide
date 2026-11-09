#include "corepch.h"

#include "Session/AgentLogging.h"

#include "Core/Base/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace Agent {

	void SendLogsToStandardError()
	{
		const auto standardError = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
		standardError->set_pattern("[%T] [agent] [%n] [%l] %v");

		for (const Core::Ref<spdlog::logger>& logger : {Core::Log::GetEngineLogger(), Core::Log::GetApplicationLogger()})
		{
			logger->sinks() = {standardError};
		}
	}
}

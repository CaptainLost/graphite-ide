#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <unordered_set>

struct ImGuiContext;

namespace spdlog {
	class logger;
}

namespace Core {

	class ImGuiErrorLog final : NonCopyable
	{
	public:
		explicit ImGuiErrorLog(spdlog::logger& logger);

		void Attach(ImGuiContext& context);

	private:
		static void OnError(ImGuiContext* context, void* userData, const char* message);

		spdlog::logger& m_Logger;
		std::unordered_set<std::string> m_ReportedMessages;
	};
}

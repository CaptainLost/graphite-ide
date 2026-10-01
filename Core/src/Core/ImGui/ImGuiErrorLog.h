#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <unordered_set>

struct ImGuiContext;

namespace spdlog {
	class logger;
}

namespace Core {

	// Writes ImGui's recoverable errors - a Begin with no End, an unbalanced PushID, a cursor
	// moved past the last item - to a log, each distinct message once.
	//
	// ImGui reports these through a callback before it decides whether to assert, so a Debug
	// build still breaks on them with the message already in the log, and a build that turns
	// the assert off recovers and leaves a line behind instead of nothing. Once, because the
	// same mistake recurs every frame and would otherwise flood the log at the frame rate.
	//
	// Must outlive the context it is attached to: the context keeps a pointer to it.
	class ImGuiErrorLog final : NonCopyable
	{
	public:
		explicit ImGuiErrorLog(spdlog::logger& logger);

		void Attach(ImGuiContext& context);

	private:
		static void OnError(ImGuiContext* context, void* self, const char* message);

		spdlog::logger& m_Logger;
		std::unordered_set<std::string> m_Reported;
	};
}

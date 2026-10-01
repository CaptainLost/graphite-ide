#include "corepch.h"

#include "Core/ImGui/ImGuiErrorLog.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/logger.h>

namespace Core {

	ImGuiErrorLog::ImGuiErrorLog(spdlog::logger& logger)
		: m_Logger(logger)
	{
	}

	void ImGuiErrorLog::Attach(ImGuiContext& context)
	{
		context.ErrorCallback = &ImGuiErrorLog::OnError;
		context.ErrorCallbackUserData = this;
	}

	void ImGuiErrorLog::OnError(ImGuiContext* context, void* self, const char* message)
	{
		auto& log = *static_cast<ImGuiErrorLog*>(self);

		if (!log.m_Reported.emplace(message).second)
		{
			return;
		}

		// The window is named only on the first report: a message that recurs in another
		// window is the same mistake, and a tab titled by its path would make every one new.
		const ImGuiWindow* window = context->CurrentWindow;
		log.m_Logger.error("ImGui error in window '{}': {}", window ? window->Name : "none",
						   message);
	}
}

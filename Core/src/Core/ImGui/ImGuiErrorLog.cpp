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

	void ImGuiErrorLog::OnError(ImGuiContext* context, void* userData, const char* message)
	{
		ImGuiErrorLog& errorLog = *static_cast<ImGuiErrorLog*>(userData);

		const auto [reportedMessage, isFirstReport] = errorLog.m_ReportedMessages.emplace(message);

		if (!isFirstReport)
		{
			return;
		}

		const ImGuiWindow* window = context->CurrentWindow;
		const char* windowName = window ? window->Name : "none";
		errorLog.m_Logger.error("ImGui error in window '{}': {}", windowName, message);
	}
}

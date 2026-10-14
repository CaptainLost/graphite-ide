#include <doctest/doctest.h>

#include "Core/Base/RingBufferSink.h"
#include "Core/ImGui/ImGuiErrorLog.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/logger.h>

#include <memory>
#include <string>
#include <vector>

using Core::ImGuiErrorLog;
using Core::RingBufferSink;

namespace {
	class IsolatedImGuiLogFixture
	{
	public:
		IsolatedImGuiLogFixture()
			: Sink(std::make_shared<RingBufferSink>(16)),
			  Logger("imgui-errors", Sink)
		{
			m_Context = ImGui::CreateContext();
			ImGui::GetIO().IniFilename = nullptr;
			// WARNING: the tooltip draws inside a frame, and these tests report from outside one.
			ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = false;
			// WARNING: ImGui's own copy of each message goes to stdout, between doctest's lines.
			ImGui::GetIO().ConfigErrorRecoveryEnableDebugLog = false;
		}

		~IsolatedImGuiLogFixture()
		{
			ImGui::DestroyContext(m_Context);
		}

		IsolatedImGuiLogFixture(const IsolatedImGuiLogFixture&) = delete;
		IsolatedImGuiLogFixture& operator=(const IsolatedImGuiLogFixture&) = delete;

		ImGuiContext& Context()
		{
			return *m_Context;
		}

		Core::Ref<RingBufferSink> Sink;
		spdlog::logger Logger;

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("ImGuiErrorLog writes a recoverable ImGui error to the log as an error")
{
	IsolatedImGuiLogFixture fixture;
	ImGuiErrorLog errorLog{fixture.Logger};
	errorLog.Attach(fixture.Context());

	ImGui::ErrorLog("Missing End()");

	const std::vector<Core::LogEntry> entries = fixture.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Level == Core::LogLevel::Error);
	CHECK(entries[0].Message.find("Missing End()") != std::string::npos);
}

TEST_CASE("ImGuiErrorLog writes an error that recurs every frame only once")
{
	IsolatedImGuiLogFixture fixture;
	ImGuiErrorLog errorLog{fixture.Logger};
	errorLog.Attach(fixture.Context());

	for (int frame = 0; frame < 3; ++frame)
	{
		ImGui::ErrorLog("Missing End()");
	}

	CHECK(fixture.Sink->TakeSnapshot().size() == 1);
}

TEST_CASE("ImGuiErrorLog writes each different error once")
{
	IsolatedImGuiLogFixture fixture;
	ImGuiErrorLog errorLog{fixture.Logger};
	errorLog.Attach(fixture.Context());

	ImGui::ErrorLog("Missing End()");
	ImGui::ErrorLog("Missing PopID()");
	ImGui::ErrorLog("Missing End()");

	const std::vector<Core::LogEntry> entries = fixture.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Message.find("Missing End()") != std::string::npos);
	CHECK(entries[1].Message.find("Missing PopID()") != std::string::npos);
}

TEST_CASE("With its assert off, ImGui recovers from a window left open and the log says so")
{
	IsolatedImGuiLogFixture fixture;
	ImGuiErrorLog errorLog{fixture.Logger};
	errorLog.Attach(fixture.Context());

	ImGuiIO& inputOutput = ImGui::GetIO();
	inputOutput.ConfigErrorRecoveryEnableAssert = false;
	inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
	inputOutput.DeltaTime = 1.0f / 60.0f;
	inputOutput.Fonts->Build();

	ImGui::NewFrame();
	ImGui::Begin("Left open");
	ImGui::EndFrame();

	const std::vector<Core::LogEntry> entries = fixture.Sink->TakeSnapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message.find("Left open") != std::string::npos);
	CHECK(fixture.Context().CurrentWindowStack.empty());
}

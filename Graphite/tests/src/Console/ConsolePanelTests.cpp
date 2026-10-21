#include <doctest/doctest.h>

#include "Console/ConsolePanel.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/Palettes.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Core/Base/RingBufferSink.h"
#include "Core/Events/EventBus.h"

#include <spdlog/logger.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <memory>
#include <string_view>

namespace {

	constexpr std::size_t kSinkCapacity = 16;
	constexpr ImVec2 kPanelSize{600.0f, 300.0f};
	constexpr std::string_view kEntriesChild = "##consoleOutput";

	const ImGuiWindow* FindEntries()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kEntriesChild) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}
}

TEST_CASE("On a light scheme the console draws warnings and errors in the scheme's diagnostic colours and info in the text colour")
{
	const Core::Ref<Core::RingBufferSink> sink = std::make_shared<Core::RingBufferSink>(kSinkCapacity);
	spdlog::logger logger("Graphite", sink);
	logger.info("Opened the folder");
	logger.warn("The server is slow to answer");
	logger.error("The server stopped");

	Graphite::ActiveScheme scheme;
	scheme.Set(Graphite::LightScheme());
	const Graphite::EditorPalette palette = Graphite::ReadEditorPalette(scheme.Get());

	ImGuiFrames frames;
	Core::EventBus events;
	Graphite::ConsolePanel panel{sink, events, scheme};

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kPanelSize);
		ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		panel.OnImGuiRender();
		ImGui::End();
	});

	const ImGuiWindow* entries = FindEntries();
	REQUIRE(entries != nullptr);

	CHECK(DrawsInColour(*entries->DrawList, ImGui::GetColorU32(ImGuiCol_Text)));
	CHECK(DrawsInColour(*entries->DrawList, palette.DiagnosticWarning));
	CHECK(DrawsInColour(*entries->DrawList, palette.DiagnosticError));
}

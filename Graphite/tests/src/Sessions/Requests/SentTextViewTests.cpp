#include <doctest/doctest.h>

#include "Sessions/Requests/SentTextView.h"

#include "ImGuiFrames.h"

#include "Core/Events/EventBus.h"

#include <imgui.h>
#include <imgui_internal.h>

using namespace Graphite;

TEST_CASE("The sent text view draws two frames in the code face and leaves the font stack balanced")
{
	ApiRequest request;
	request.Key = "r1";
	request.Sent.push_back({.Kind = SentKind::Reminder, .Label = "System", .Text = "first line\nsecond line"});

	const SessionPalette palette;
	Core::EventBus events;
	SentTextView view;
	ImGuiFrames frames;

	for (int frame = 0; frame < 2; ++frame)
	{
		frames.Next([&] {
			ImGui::Begin("sent");
			view.Draw(request, 1, std::nullopt, std::nullopt, palette, events);
			ImGui::End();
		});
	}

	CHECK(ImGui::GetCurrentContext()->FontStack.Size == 0);
}

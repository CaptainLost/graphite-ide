#include <doctest/doctest.h>

#include "Sessions/Conversation/ConversationView.h"

#include "ImGuiFrames.h"
#include "TranscriptFixtures.h"

#include "Core/Events/EventBus.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

using namespace Graphite;

namespace {

	constexpr float kInterfaceSizeBelowCode = 14.0f;
	constexpr float kHarnessSizeBelowCode = 13.0f;
	constexpr const char* kCodeChildMarker = "##code";

	float HeightOfCodeBlockDrawnAtBase(float fontSizeBase)
	{
		Session session;
		session.Append(ParseTranscript(
							Fixtures::UserPrompt("show me") +
							Fixtures::Assistant("r1", Fixtures::TextBlock("Here:\n\n```\nint x;\n```\n"),
												Fixtures::Usage(1, 1), "claude-opus-5", "2026-09-17T09:00:01.000Z",
												"end_turn"))
						   .Records);

		const Pricing prices = Pricing::Defaults();
		const SubagentUsage subagents;
		const SessionPalette palette;
		const ConversationContext context{.Prices = prices, .Subagents = subagents, .Palette = palette};

		Core::EventBus events;
		ConversationView view;
		ImGuiFrames frames;

		ImGui::GetStyle().FontSizeBase = fontSizeBase;

		float height = 0.0f;

		for (int frame = 0; frame < 3; ++frame)
		{
			frames.Next([&] {
				ImGui::SetNextWindowSize(ImVec2(600.0f, 600.0f));
				ImGui::Begin("conversation");
				view.Draw(session, context, events);
				ImGui::End();

				for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
				{
					if (std::string{window->Name}.find(kCodeChildMarker) != std::string::npos)
					{
						height = window->Size.y;
					}
				}
			});
		}

		return height;
	}
}

TEST_CASE("A conversation code block keeps the code size whatever the interface size is")
{
	const float atInterfaceSize = HeightOfCodeBlockDrawnAtBase(kInterfaceSizeBelowCode);
	const float atHarnessSize = HeightOfCodeBlockDrawnAtBase(kHarnessSizeBelowCode);

	CHECK(atInterfaceSize > 0.0f);
	CHECK(atInterfaceSize == doctest::Approx(atHarnessSize));
}

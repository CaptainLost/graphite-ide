#include <doctest/doctest.h>

#include "Sessions/Conversation/ConversationView.h"

#include "ImGuiFrames.h"
#include "TranscriptFixtures.h"

#include "Core/Events/EventBus.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <utility>

using namespace Graphite;

namespace {

	constexpr float kInterfaceSizeBelowCode = 14.0f;
	constexpr float kHarnessSizeBelowCode = 13.0f;
	constexpr const char* kCodeChildMarker = "##code";
	constexpr const char* kConversationChildMarker = "##conversation";
	constexpr float kPointerStepDown = 2.0f;
	constexpr float kHalf = 0.5f;
	constexpr int kSettleFrames = 3;
	constexpr ImGuiID kNoItem = 0;

	const ImGuiWindow* ConversationWindow()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string{window->Name}.find(kConversationChildMarker) != std::string::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	struct Pointer
	{
		float X = 0.0f;
		float Y = 0.0f;
		ImGuiID Over = kNoItem;
	};

	// WARNING: ImGui keeps no item's position past its frame, so the pointer moves down the
	// conversation until it is over an item other than the one it last clicked.
	bool ClickNextItemDown(ImGuiFrames& frames, const std::function<void()>& draw, Pointer& pointer)
	{
		const ImGuiWindow* window = ConversationWindow();

		if (window == nullptr)
		{
			return false;
		}

		const ImGuiID passed = pointer.Over;
		const float bottom = window->Pos.y + window->Size.y;

		for (; pointer.Y < bottom; pointer.Y += kPointerStepDown)
		{
			ImGui::GetIO().AddMousePosEvent(pointer.X, pointer.Y);
			frames.Next(draw);

			const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;

			if (hovered != kNoItem && hovered != passed)
			{
				pointer.Over = hovered;
				frames.Click(ImVec2(pointer.X, pointer.Y), draw);
				return true;
			}
		}

		return false;
	}

	float SettledContentHeight(ImGuiFrames& frames, const std::function<void()>& draw)
	{
		for (int frame = 0; frame < kSettleFrames; ++frame)
		{
			frames.Next(draw);
		}

		const ImGuiWindow* window = ConversationWindow();
		return window == nullptr ? 0.0f : window->ContentSize.y;
	}

	float GrowthOfOpenedStepAtBase(float fontSizeBase, const std::string& toolName, const nlohmann::json& input)
	{
		Session session;
		session.Append(ParseTranscript(Fixtures::UserPrompt("run it") +
									   Fixtures::Assistant("r1", Fixtures::ToolUseBlock("t1", toolName, input),
														   Fixtures::Usage(1, 1)) +
									   Fixtures::ToolResult("t1", "first\nsecond\nthird"))
						   .Records);

		const Pricing prices = Pricing::Defaults();
		const SubagentUsage subagents;
		const SessionPalette palette;
		const ConversationContext context{.Prices = prices, .Subagents = subagents, .Palette = palette};

		Core::EventBus events;
		ConversationView view;
		ImGuiFrames frames;

		ImGui::GetStyle().FontSizeBase = fontSizeBase;

		const std::function<void()> draw = [&] {
			ImGui::SetNextWindowSize(ImVec2(600.0f, 600.0f));
			ImGui::Begin("conversation");
			view.Draw(session, context, events);
			ImGui::End();
		};

		SettledContentHeight(frames, draw);

		const ImGuiWindow* window = ConversationWindow();
		REQUIRE(window != nullptr);

		Pointer pointer{.X = window->Pos.x + window->Size.x * kHalf, .Y = window->Pos.y};

		REQUIRE(ClickNextItemDown(frames, draw, pointer));
		const float withStepClosed = SettledContentHeight(frames, draw);

		REQUIRE(ClickNextItemDown(frames, draw, pointer));
		const float withStepOpened = SettledContentHeight(frames, draw);

		return withStepOpened - withStepClosed;
	}

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

TEST_CASE("An opened tool step keeps its input, its edit lines and its result at the code size whatever the interface size is")
{
	const nlohmann::json commandInput = {{"command", "ls"}};
	const nlohmann::json editInput = {{"old_string", "old one\nold two"}, {"new_string", "new one\nnew two"}};

	for (const auto& [toolName, input] :
		 {std::pair<std::string, nlohmann::json>{"Bash", commandInput}, std::pair<std::string, nlohmann::json>{"Edit", editInput}})
	{
		CAPTURE(toolName);

		const float atInterfaceSize = GrowthOfOpenedStepAtBase(kInterfaceSizeBelowCode, toolName, input);
		const float atHarnessSize = GrowthOfOpenedStepAtBase(kHarnessSizeBelowCode, toolName, input);

		CHECK(atInterfaceSize > 0.0f);
		CHECK(atInterfaceSize == doctest::Approx(atHarnessSize));
	}
}

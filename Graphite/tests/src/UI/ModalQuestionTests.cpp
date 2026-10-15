#include <doctest/doctest.h>

#include "UI/ModalGate.h"
#include "UI/ModalQuestion.h"

#include "ImGuiFrames.h"

#include <imgui.h>

#include <string>

namespace {
	struct Keyed
	{
		std::string Subject;
		std::string Text;

		bool IsSameQuestion(const Keyed& other) const { return Subject == other.Subject; }
	};

	struct Plain
	{
		int Value = 0;

		bool operator==(const Plain&) const = default;
	};
}

TEST_CASE("ModalQuestion takes two payloads for one question when the payload says they are")
{
	Graphite::ModalGate gate;
	Graphite::ModalQuestion<Keyed> question{gate, "Keyed"};

	question.Ask({"a.txt", "first wording"});

	CHECK(question.Asked({"a.txt", "other wording"}));
	CHECK_FALSE(question.Asked({"b.txt", "first wording"}));
}

TEST_CASE("ModalQuestion compares a payload without IsSameQuestion by equality")
{
	Graphite::ModalGate gate;
	Graphite::ModalQuestion<Plain> question{gate, "Plain"};

	question.Ask({1});

	CHECK(question.Asked({1}));
	CHECK_FALSE(question.Asked({2}));
}

TEST_CASE("ModalQuestion closes the popup of a question withdrawn while on screen, and Begin answers false for it")
{
	ImGuiFrames frames;
	Graphite::ModalGate gate;
	Graphite::ModalQuestion<Plain> question{gate, "Plain question"};
	bool beganWithNothingAsked = false;

	const auto draw = [&] {
		ImGui::Begin("Host");

		if (question.Begin())
		{
			beganWithNothingAsked = beganWithNothingAsked || question.GetCurrent() == nullptr;
			question.End();
		}

		ImGui::End();
	};

	question.Ask({1});
	frames.Next(draw);
	REQUIRE(ImGuiFrames::IsPopupNamedOpen("Plain question"));

	question.Withdraw();
	frames.Next(draw);
	frames.Next(draw);

	CHECK_FALSE(beganWithNothingAsked);
	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Plain question"));
	CHECK_FALSE(gate.IsBusy());
}

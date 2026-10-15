#include <doctest/doctest.h>

#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/ModalQuestion.h"

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

TEST_CASE("ModalQuestion's Begin hands over the question to draw while its popup is open")
{
	ImGuiFrames frames;
	Graphite::ModalGate gate;
	Graphite::ModalQuestion<Plain> question{gate, "Plain question"};
	int drawnValue = 0;

	question.Ask({7});

	frames.Next([&] {
		if (const Plain* asked = question.Begin())
		{
			drawnValue = asked->Value;
			question.End();
		}
	});

	CHECK(drawnValue == 7);
}

TEST_CASE("ModalQuestion's AskOnce leaves a question already asked as it is")
{
	ImGuiFrames frames;
	Graphite::ModalGate gate;
	Graphite::ModalQuestion<Plain> question{gate, "Plain question"};

	question.AskOnce({1});
	question.AskOnce({1});

	frames.Next([&] {
		if (question.Begin())
		{
			question.Answer();
			question.End();
		}
	});

	CHECK_FALSE(question.IsAsked());
}

TEST_CASE("ModalQuestion closes the popup of a question withdrawn while on screen, and Begin hands over nothing for it")
{
	ImGuiFrames frames;
	Graphite::ModalGate gate;
	Graphite::ModalQuestion<Plain> question{gate, "Plain question"};
	int framesBegun = 0;

	const auto draw = [&] {
		ImGui::Begin("Host");

		if (question.Begin())
		{
			++framesBegun;
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

	CHECK(framesBegun == 1);
	CHECK_FALSE(ImGuiFrames::IsPopupNamedOpen("Plain question"));
	CHECK_FALSE(gate.IsBusy());
}

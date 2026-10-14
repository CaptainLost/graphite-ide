#include <doctest/doctest.h>

#include "UI/ModalGate.h"
#include "UI/ModalQuestion.h"

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

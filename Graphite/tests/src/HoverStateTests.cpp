#include <doctest/doctest.h>

#include "Editor/Hover/HoverState.h"

#include "Core/Text/DocumentSet.h"

using Graphite::HoverQuestion;
using Graphite::HoverState;
using Graphite::LanguageServices;

namespace {
	HoverQuestion QuestionAt(std::size_t from)
	{
		HoverQuestion question;
		question.Path = "C:/project/main.cpp";
		question.TokenFrom = from;
		question.TokenTo = from + 3;
		return question;
	}
}

TEST_CASE("A failed hover answer leaves what was held, so the token is asked about again")
{
	HoverState hover;

	LanguageServices::Answer answered;
	answered.Text = "### function `Sum`";
	Graphite::HoldHoverAnswer(hover, QuestionAt(1), answered);

	LanguageServices::Answer failed;
	failed.Failed = true;
	Graphite::HoldHoverAnswer(hover, QuestionAt(9), failed);

	REQUIRE(hover.InfoFor.has_value());
	CHECK(*hover.InfoFor == QuestionAt(1));
	REQUIRE(hover.Info.has_value());
	CHECK((hover.Info->Parsed || !hover.Info->Raw.empty()));
}

TEST_CASE("An empty hover answer is held: the question has been answered, with nothing")
{
	HoverState hover;

	Graphite::HoldHoverAnswer(hover, QuestionAt(1), LanguageServices::Answer{});

	REQUIRE(hover.InfoFor.has_value());
	CHECK(*hover.InfoFor == QuestionAt(1));
	REQUIRE(hover.Info.has_value());
	CHECK_FALSE(hover.Info->Parsed);
	CHECK(hover.Info->Raw.empty());
}

TEST_CASE("A failed preview answer leaves what was held, and an empty one is held as unreadable")
{
	HoverState hover;
	Core::DocumentSet documents;

	LanguageServices::Answer failed;
	failed.Failed = true;
	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), failed, documents);

	CHECK_FALSE(hover.PreviewFor.has_value());
	CHECK_FALSE(hover.Preview.has_value());

	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), LanguageServices::Answer{}, documents);

	REQUIRE(hover.PreviewFor.has_value());
	CHECK(*hover.PreviewFor == QuestionAt(1));
	REQUIRE(hover.Preview.has_value());
	CHECK(hover.Preview->Unreadable);
}

TEST_CASE("A failed answer is remembered as failed for its question until one succeeds")
{
	HoverState hover;
	Core::DocumentSet documents;

	LanguageServices::Answer failed;
	failed.Failed = true;

	Graphite::HoldHoverAnswer(hover, QuestionAt(1), failed);
	REQUIRE(hover.InfoFailedFor.has_value());
	CHECK(*hover.InfoFailedFor == QuestionAt(1));

	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), failed, documents);
	REQUIRE(hover.PreviewFailedFor.has_value());
	CHECK(*hover.PreviewFailedFor == QuestionAt(1));

	Graphite::HoldHoverAnswer(hover, QuestionAt(1), LanguageServices::Answer{});
	CHECK_FALSE(hover.InfoFailedFor.has_value());

	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), LanguageServices::Answer{}, documents);
	CHECK_FALSE(hover.PreviewFailedFor.has_value());
}

#include <doctest/doctest.h>

#include "Editor/Hover/HoverState.h"

#include "Core/Text/DocumentSet.h"
#include "Language/Server/ClangdHover.h"

#include <ostream>
#include <string>
#include <vector>

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
	answered.Hover = Graphite::ParseClangdHover("### function `Sum`");
	Graphite::HoldHoverAnswer(hover, QuestionAt(1), answered);

	LanguageServices::Answer failed;
	failed.Failed = true;
	Graphite::HoldHoverAnswer(hover, QuestionAt(9), failed);

	REQUIRE(hover.InfoFor.has_value());
	CHECK(*hover.InfoFor == QuestionAt(1));
	REQUIRE(hover.Info.has_value());
	CHECK((hover.Info->Parsed || !hover.Info->Raw.empty()));
}

TEST_CASE("A hover answer is held as its adapter read it")
{
	HoverState hover;

	LanguageServices::Answer answered;
	answered.Hover.Parsed = true;
	answered.Hover.Declaration = "int Sum()";
	Graphite::HoldHoverAnswer(hover, QuestionAt(1), answered);

	REQUIRE(hover.Info.has_value());
	CHECK(hover.Info->Declaration == "int Sum()");
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

TEST_CASE("A preview is coloured once for a tab width and again only when the width changes")
{
	HoverState hover;
	hover.Preview = Graphite::DefinitionPreview{.Path = "C:/project/Widget.cs", .FirstLine = 0, .Lines = {"\tpublic int Size;"}};

	const Graphite::ColouredPreview& first = Graphite::ColouredPreviewOf(hover, 4);

	REQUIRE(first.Tokens.size() == 1);
	CHECK_FALSE(first.Tokens[0].empty());
	CHECK(first.Dedented.Lines == std::vector<std::string>{"public int Size;"});

	hover.PreviewColours->Dedented.Lines[0] = "marker";

	CHECK(Graphite::ColouredPreviewOf(hover, 4).Dedented.Lines[0] == "marker");

	const Graphite::ColouredPreview& wider = Graphite::ColouredPreviewOf(hover, 8);

	CHECK(wider.TabWidth == 8);
	CHECK(wider.Dedented.Lines[0] == "public int Size;");
}

TEST_CASE("A hover that could not reach its server keeps the server's state to show, and one that erred keeps nothing")
{
	HoverState hover;
	Core::DocumentSet documents;

	LanguageServices::Answer notInstalled;
	notInstalled.Failed = true;
	notInstalled.FailedBecause = Graphite::ReplyFailure::ServerUnavailable;
	notInstalled.ServerAtFailure = Graphite::ServerStatus{.State = Graphite::ServerState::NotInstalled, .Name = "clangd"};

	Graphite::HoldHoverAnswer(hover, QuestionAt(1), notInstalled);
	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), notInstalled, documents);

	CHECK(hover.InfoFailureText == "clangd not installed");
	CHECK(hover.PreviewFailureText == "clangd not installed");

	LanguageServices::Answer erred;
	erred.Failed = true;
	erred.FailedBecause = Graphite::ReplyFailure::ServerError;
	erred.ServerAtFailure = Graphite::ServerStatus{.State = Graphite::ServerState::Ready, .Name = "clangd"};

	Graphite::HoldHoverAnswer(hover, QuestionAt(1), erred);

	CHECK(hover.InfoFailureText.empty());
}

TEST_CASE("A hover under a server turned off, not chosen or kept from an untrusted folder has nothing to say")
{
	for (const Graphite::ServerState state :
		 {Graphite::ServerState::Off, Graphite::ServerState::NotConfigured, Graphite::ServerState::Restricted})
	{
		LanguageServices::Answer unavailable;
		unavailable.Failed = true;
		unavailable.FailedBecause = Graphite::ReplyFailure::ServerUnavailable;
		unavailable.ServerAtFailure = Graphite::ServerStatus{.State = state, .Name = "clangd"};

		CAPTURE(static_cast<int>(state));
		CHECK(Graphite::HoverFailureText(unavailable).empty());
	}
}

TEST_CASE("A hover waits with the server's own words while it comes up, and is loading otherwise")
{
	CHECK(Graphite::HoverLoadingText(Graphite::ServerStatus{.State = Graphite::ServerState::Starting, .Name = "clangd"}) ==
		  "Waiting for clangd to start");
	CHECK(Graphite::HoverLoadingText(Graphite::ServerStatus{.State = Graphite::ServerState::Ready, .Name = "clangd"}) ==
		  "Loading\xE2\x80\xA6");
}

TEST_CASE("A preview answered with no location is remembered as no definition found")
{
	HoverState hover;
	Core::DocumentSet documents;

	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), LanguageServices::Answer{}, documents);

	CHECK(hover.NoDefinitionFound);
}

TEST_CASE("A new preview answer drops the colours of the last one")
{
	HoverState hover;
	hover.Preview = Graphite::DefinitionPreview{.Path = "C:/project/Widget.cs", .FirstLine = 0, .Lines = {"public int Size;"}};
	Graphite::ColouredPreviewOf(hover, 4);
	REQUIRE(hover.PreviewColours.has_value());

	const Core::DocumentSet documents;
	Graphite::HoldPreviewAnswer(hover, QuestionAt(1), LanguageServices::Answer{}, documents);

	CHECK_FALSE(hover.PreviewColours.has_value());
}

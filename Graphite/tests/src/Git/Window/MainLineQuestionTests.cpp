#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Window/MainLineQuestion.h"
#include "Theme/BuiltInSchemes.h"

#include <imgui.h>

#include <string>

namespace {

	Core::LineRef RefNamed(const std::string& remote, const std::string& shortName)
	{
		Core::LineRef ref;
		ref.Remote = remote;
		ref.ShortName = shortName;
		ref.FullName = remote.empty() ? "refs/heads/" + shortName : "refs/remotes/" + remote + "/" + shortName;
		ref.Kind = remote.empty() ? Core::RefKind::Local : Core::RefKind::Remote;
		return ref;
	}

	Graphite::LineRead GuessedRead(const Core::LineRef& guess)
	{
		Graphite::LineRead read;
		read.Refs = {RefNamed("", "feature"), guess};

		Core::LineBase base;
		base.Ref = 1;
		base.Reason = Core::BaseReason::MainLineGuess;
		read.Model.Bases = {base};
		read.Model.MainLineGuessed = true;

		return read;
	}
}

TEST_CASE("Git window: the main line is asked only while it is guessed")
{
	const Graphite::LineRead guessed = GuessedRead(RefNamed("", "develop"));

	const std::optional<Graphite::MainLineAsk> ask = Graphite::MainLineToAsk(&guessed, false);
	REQUIRE(ask.has_value());
	CHECK(ask->GuessRef == "refs/heads/develop");
	CHECK(ask->GuessName == "develop");

	Graphite::LineRead answered = guessed;
	answered.Model.MainLineGuessed = false;
	answered.Model.Bases[0].Reason = Core::BaseReason::MainLineByUser;
	CHECK_FALSE(Graphite::MainLineToAsk(&answered, false).has_value());

	CHECK_FALSE(Graphite::MainLineToAsk(nullptr, false).has_value());
}

TEST_CASE("Git window: the guess of a remote branch is named with its remote")
{
	const Graphite::LineRead guessed = GuessedRead(RefNamed("origin", "develop"));

	const std::optional<Graphite::MainLineAsk> ask = Graphite::MainLineToAsk(&guessed, false);
	REQUIRE(ask.has_value());
	CHECK(ask->GuessRef == "refs/remotes/origin/develop");
	CHECK(ask->GuessName == "origin/develop");
}

TEST_CASE("Git window: not now hides the question")
{
	const Graphite::LineRead guessed = GuessedRead(RefNamed("", "develop"));

	CHECK_FALSE(Graphite::MainLineToAsk(&guessed, true).has_value());
}

TEST_CASE("Git window: a read that names no guessed base asks nothing")
{
	Graphite::LineRead odd = GuessedRead(RefNamed("", "develop"));
	odd.Model.Bases[0].Reason = Core::BaseReason::ServerDefault;
	CHECK_FALSE(Graphite::MainLineToAsk(&odd, false).has_value());

	Graphite::LineRead outOfRange = GuessedRead(RefNamed("", "develop"));
	outOfRange.Model.Bases[0].Ref = 9;
	CHECK_FALSE(Graphite::MainLineToAsk(&outOfRange, false).has_value());

	Graphite::LineRead empty;
	empty.Model.MainLineGuessed = true;
	CHECK_FALSE(Graphite::MainLineToAsk(&empty, false).has_value());
}

namespace {

	constexpr float kWindowWidth = 1000.0f;
	constexpr float kEdge = 24.0f;

	struct QuestionHarness
	{
		ImGuiFrames Frames;
		Graphite::MainLineAsk Ask{"refs/heads/develop", "develop"};
		Graphite::ResolvedScheme Scheme = Graphite::DarkScheme();
		Graphite::MainLineAnswer Last = Graphite::MainLineAnswer::None;
		float Height = 0.0f;

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kWindowWidth, 300.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("MainLineQuestionHarness", nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

			const float top = ImGui::GetCursorPosY();
			const Graphite::MainLineAnswer answer = Graphite::DrawMainLineQuestion(Ask, Graphite::ReadWindowBarColours(Scheme));
			Height = ImGui::GetCursorPosY() - top;

			if (answer != Graphite::MainLineAnswer::None)
			{
				Last = answer;
			}

			ImGui::End();
			ImGui::PopStyleVar();
		}

		void Next() { Frames.Next([&] { Draw(); }); }
		void Click(ImVec2 at) { Frames.Click(at, [&] { Draw(); }); }
	};
}

TEST_CASE("Git window: the question bar answers Not now from its right-hand button")
{
	QuestionHarness harness;
	harness.Next();
	harness.Next();
	REQUIRE(harness.Height > 0.0f);

	harness.Click(ImVec2(kWindowWidth - kEdge, harness.Height * 0.5f));

	CHECK(harness.Last == Graphite::MainLineAnswer::NotNow);
}

TEST_CASE("Git window: the question bar answers Use from the button beside it")
{
	QuestionHarness harness;
	harness.Next();
	harness.Next();

	// Not now is the narrower of the two; the guess's button lies left of it.
	harness.Click(ImVec2(kWindowWidth - 140.0f, harness.Height * 0.5f));

	CHECK(harness.Last == Graphite::MainLineAnswer::UseGuess);
}

TEST_CASE("Git window: a repository whose branches all share one name is not asked for its main line")
{
	Graphite::LineRead single = GuessedRead(RefNamed("", "main"));
	single.Refs = {RefNamed("", "main")};
	single.Model.Bases[0].Ref = 0;
	CHECK_FALSE(Graphite::MainLineToAsk(&single, false).has_value());

	Graphite::LineRead tracked = GuessedRead(RefNamed("origin", "main"));
	tracked.Refs = {RefNamed("", "main"), RefNamed("origin", "main")};
	CHECK_FALSE(Graphite::MainLineToAsk(&tracked, false).has_value());
}

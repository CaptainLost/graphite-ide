#include <doctest/doctest.h>

#include "Editor/Navigation/NavigationBubble.h"

#include <filesystem>
#include <optional>
#include <string>

namespace {

	const std::filesystem::path kSource{"C:/project/main.cpp"};
	constexpr std::size_t kAskedAt = 120;
	constexpr std::size_t kRevision = 7;
	constexpr double kStart = 50.0;
	constexpr double kJustBefore = 0.01;

	Graphite::NavigationBubble WaitingBubble(Graphite::NavigationCommand command = Graphite::NavigationCommand::Definition)
	{
		Graphite::NavigationBubble bubble;
		bubble.BeginWaiting(command, kSource, kAskedAt, kRevision, "Finding definition", kStart);
		return bubble;
	}

	Graphite::ReaderPosition StillThere()
	{
		return Graphite::ReaderPosition{.ActiveDocument = kSource, .BubbleDocumentShown = true, .CaretOffset = kAskedAt, .Revision = kRevision};
	}
}

TEST_CASE("A navigation wait shows only once it has lasted a fifth of a second")
{
	const Graphite::NavigationBubble bubble = WaitingBubble();

	CHECK_FALSE(bubble.ShownAt(kStart).has_value());
	CHECK_FALSE(bubble.ShownAt(kStart + Graphite::NavigationBubble::kShowAfterSeconds - kJustBefore).has_value());
	REQUIRE(bubble.NextChangeAfter(kStart).has_value());
	CHECK(*bubble.NextChangeAfter(kStart) == doctest::Approx(kStart + Graphite::NavigationBubble::kShowAfterSeconds));

	const std::optional<Graphite::ShownBubble> shown = bubble.ShownAt(kStart + Graphite::NavigationBubble::kShowAfterSeconds);

	REQUIRE(shown.has_value());
	CHECK(shown->Text == "Finding definition");
	CHECK(shown->Waiting);
}

TEST_CASE("How a navigation command ended shows at once and for a couple of seconds")
{
	Graphite::NavigationBubble bubble = WaitingBubble();
	bubble.End(Graphite::NavigationCommand::Definition, "No definition found", kStart + kJustBefore);

	const std::optional<Graphite::ShownBubble> shown = bubble.ShownAt(kStart + kJustBefore);

	REQUIRE(shown.has_value());
	CHECK(shown->Text == "No definition found");
	CHECK_FALSE(shown->Waiting);

	const double endsAt = kStart + kJustBefore + Graphite::NavigationBubble::kOutcomeSeconds;

	CHECK(bubble.ShownAt(endsAt - kJustBefore).has_value());
	CHECK_FALSE(bubble.ShownAt(endsAt).has_value());
	REQUIRE(bubble.NextChangeAfter(kStart + 1.0).has_value());
	CHECK(*bubble.NextChangeAfter(kStart + 1.0) == doctest::Approx(endsAt));
}

TEST_CASE("A closed bubble ends nothing and shows nothing")
{
	Graphite::NavigationBubble bubble = WaitingBubble();
	bubble.Close(Graphite::NavigationCommand::Definition);
	bubble.End(Graphite::NavigationCommand::Definition, "No definition found", kStart);

	CHECK_FALSE(bubble.IsOpen());
	CHECK_FALSE(bubble.ShownAt(kStart + 1.0).has_value());
}

TEST_CASE("A bubble stays while the reader stays where the command was given")
{
	Graphite::NavigationBubble bubble = WaitingBubble();

	CHECK_FALSE(bubble.AbandonIfReaderMovedOn(StillThere()).has_value());
	CHECK(bubble.IsWaitingFor(Graphite::NavigationCommand::Definition));
}

TEST_CASE("Moving the caret, typing, Escape or another document abandons the wait and names the command to cancel")
{
	Graphite::ReaderPosition moved = StillThere();
	moved.CaretOffset = kAskedAt + 1;

	Graphite::ReaderPosition typed = StillThere();
	typed.Revision = kRevision + 1;

	Graphite::ReaderPosition escaped = StillThere();
	escaped.EscapePressed = true;

	Graphite::ReaderPosition hidden = StillThere();
	hidden.BubbleDocumentShown = false;

	for (const Graphite::ReaderPosition& reader : {moved, typed, escaped, hidden})
	{
		Graphite::NavigationBubble bubble = WaitingBubble(Graphite::NavigationCommand::SwitchSourceHeader);

		CHECK(bubble.AbandonIfReaderMovedOn(reader) == Graphite::NavigationCommand::SwitchSourceHeader);
		CHECK_FALSE(bubble.IsOpen());
	}
}

TEST_CASE("A command given in a pane that is not active yet waits for that pane, and ends when another becomes active")
{
	Graphite::NavigationBubble bubble = WaitingBubble();

	Graphite::ReaderPosition otherPaneStillActive = StillThere();
	otherPaneStillActive.ActiveDocument = std::filesystem::path{"C:/project/other.cpp"};

	CHECK_FALSE(bubble.AbandonIfReaderMovedOn(otherPaneStillActive).has_value());
	CHECK_FALSE(bubble.AbandonIfReaderMovedOn(StillThere()).has_value());
	CHECK(bubble.IsWaitingFor(Graphite::NavigationCommand::Definition));

	CHECK(bubble.AbandonIfReaderMovedOn(otherPaneStillActive) == Graphite::NavigationCommand::Definition);
	CHECK_FALSE(bubble.IsOpen());
}

TEST_CASE("How one command ended neither closes nor replaces the bubble of the other")
{
	Graphite::NavigationBubble bubble = WaitingBubble(Graphite::NavigationCommand::SwitchSourceHeader);

	bubble.End(Graphite::NavigationCommand::Definition, "No definition found", kStart);
	bubble.Close(Graphite::NavigationCommand::Definition);

	CHECK(bubble.IsWaitingFor(Graphite::NavigationCommand::SwitchSourceHeader));

	bubble.End(Graphite::NavigationCommand::SwitchSourceHeader, "No header/source counterpart", kStart);

	REQUIRE(bubble.ShownAt(kStart).has_value());
	CHECK(bubble.ShownAt(kStart)->Text == "No header/source counterpart");
}

TEST_CASE("Moving on from an ended bubble closes it with nothing to cancel")
{
	Graphite::NavigationBubble bubble = WaitingBubble();
	bubble.End(Graphite::NavigationCommand::Definition, "clangd did not answer", kStart);

	Graphite::ReaderPosition moved = StillThere();
	moved.CaretOffset = 0;

	CHECK_FALSE(bubble.AbandonIfReaderMovedOn(moved).has_value());
	CHECK_FALSE(bubble.IsOpen());
}

TEST_CASE("The waiting text changes only while the bubble waits")
{
	Graphite::NavigationBubble bubble = WaitingBubble();
	bubble.SetWaitingText("Waiting for Roslyn to load Graphite.csproj");

	CHECK(bubble.ShownAt(kStart + 1.0)->Text == "Waiting for Roslyn to load Graphite.csproj");

	bubble.End(Graphite::NavigationCommand::Definition, "No definition found", kStart + 1.0);
	bubble.SetWaitingText("Finding definition");

	CHECK(bubble.ShownAt(kStart + 1.0)->Text == "No definition found");
}

TEST_CASE("No definition from a server still indexing says the index is not complete")
{
	CHECK(Graphite::NoDefinitionFoundText(Graphite::ServerStatus{.State = Graphite::ServerState::Ready, .Name = "clangd"}) ==
		  "No definition found");

	const Graphite::ServerStatus indexing{.State = Graphite::ServerState::Indexing, .Name = "clangd", .Percent = 40};

	CHECK(Graphite::NoDefinitionFoundText(indexing) == "No definition found yet \xC2\xB7 clangd is still indexing (40%)");
}

TEST_CASE("A caret scrolled out of its editor's surface has no bubble")
{
	Graphite::CaretPlacement caret{.X = 100.0f,
								   .Y = 200.0f,
								   .LineHeight = 20.0f,
								   .SurfaceMin = ImVec2(50.0f, 100.0f),
								   .SurfaceMax = ImVec2(800.0f, 600.0f)};

	CHECK(Graphite::IsCaretInSurface(caret));

	caret.Y = 90.0f;
	CHECK_FALSE(Graphite::IsCaretInSurface(caret));

	caret.Y = 590.0f;
	CHECK_FALSE(Graphite::IsCaretInSurface(caret));

	caret.Y = 200.0f;
	caret.X = 900.0f;
	CHECK_FALSE(Graphite::IsCaretInSurface(caret));
}

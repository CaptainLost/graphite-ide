#include <doctest/doctest.h>

#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include "ImGuiFrames.h"

#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Encoding.h"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace {
	float Columns(std::string_view text)
	{
		return static_cast<float>(Core::CharacterCount(text));
	}
}

TEST_CASE("Text that fits is not cut")
{
	CHECK_FALSE(Graphite::CutToFit("fix: a bug", 10.0f, 1.0f, Columns).has_value());
	CHECK_FALSE(Graphite::CutToFit("", 0.0f, 1.0f, Columns).has_value());
}

TEST_CASE("Text too long keeps as much as fits beside the ellipsis")
{
	const std::optional<std::size_t> cut =
		Graphite::CutToFit("abcdefghijklmnop", 10.0f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(*cut == 9);
}

TEST_CASE("A cut drops the spaces it would otherwise end on")
{
	const std::optional<std::size_t> cut =
		Graphite::CutToFit("fix: end the changes", 14.0f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(std::string_view("fix: end the changes").substr(0, *cut) == "fix: end the");
}

TEST_CASE("A cut never splits a multi-byte UTF-8 character")
{
	const std::string_view name = "Ren\xC3\xA9 D\xC3\xA9j\xC3\xA0 Vu";

	const std::optional<std::size_t> cut = Graphite::CutToFit(name, 5.0f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(name.substr(0, *cut) == "Ren\xC3\xA9");
}

TEST_CASE("Text that starts mid-character is cut by width all the same")
{
	const std::string_view text = "\xB0\xB0\xB0"
								  "abc";

	const auto bytes = [](std::string_view piece) { return static_cast<float>(piece.size()); };

	const std::optional<std::size_t> cut = Graphite::CutToFit(text, 3.0f, 1.0f, bytes);

	REQUIRE(cut.has_value());
	CHECK(*cut == 0);
}

TEST_CASE("A cell narrower than the ellipsis keeps nothing rather than underflowing")
{
	const std::optional<std::size_t> cut = Graphite::CutToFit("Ada Lovelace", 0.5f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(*cut == 0);
}

TEST_CASE("Text that fits its room is kept whole")
{
	ImGuiFrames frames;
	std::string fitted;
	bool cut = true;

	frames.Next([&] { fitted = Graphite::FitToWidth("abc", Core::TextWidth("abc"), &cut); });

	CHECK(fitted == "abc");
	CHECK_FALSE(cut);
}

TEST_CASE("Text too long for its room keeps what fits and ends in an ellipsis")
{
	ImGuiFrames frames;
	std::string fitted;
	bool cut = false;

	frames.Next([&] {
		const std::string room = std::string{"abc"} + std::string{Graphite::kEllipsis};
		fitted = Graphite::FitToWidth("abcdefgh", Core::TextWidth(room), &cut);
	});

	CHECK(fitted == std::string{"abc"} + std::string{Graphite::kEllipsis});
	CHECK(cut);
}

TEST_CASE("A room narrower than the ellipsis by more than a pixel gets nothing rather than part of an ellipsis")
{
	ImGuiFrames frames;
	std::string fitted = "not drawn";
	bool cut = false;

	frames.Next([&] { fitted = Graphite::FitToWidth("abcdefgh", Core::TextWidth(Graphite::kEllipsis) * 0.5f, &cut); });

	CHECK(fitted.empty());
	CHECK(cut);
}

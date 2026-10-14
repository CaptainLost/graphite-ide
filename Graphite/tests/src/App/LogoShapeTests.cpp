#include <doctest/doctest.h>

#include "App/LogoShape.h"
#include "App/LogoSvg.h"

#include <string>
#include <string_view>

namespace {
	using Graphite::LogoShape;
	using Graphite::ParseLogoSvg;

	std::string Svg(std::string_view body)
	{
		return std::string(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">)svg") +
			   std::string(body) + "</svg>";
	}
}

TEST_CASE("LogoShape: the embedded logo is its six faces without the disc")
{
	const LogoShape shape =
		ParseLogoSvg(std::string_view(reinterpret_cast<const char*>(Graphite::kLogoSvg), Graphite::kLogoSvgSize));

	REQUIRE(shape.Faces.size() == 6);

	CHECK(shape.Min.x == doctest::Approx(221.8f));
	CHECK(shape.Max.x == doctest::Approx(802.2f));
	CHECK(shape.Min.y == doctest::Approx(176.9f));
	CHECK(shape.Max.y == doctest::Approx(847.1f));

	CHECK(shape.Faces[0].Points.size() == 6);
	CHECK(shape.Faces[0].From.x == doctest::Approx(248.0f));
	CHECK(shape.Faces[0].To.y == doctest::Approx(811.0f));
	CHECK(shape.Faces[0].FromColour == IM_COL32(0xd1, 0xd2, 0xd6, 0xff));
	CHECK(shape.Faces[0].ToColour == IM_COL32(0x9d, 0xa0, 0xab, 0xff));
}

TEST_CASE("LogoShape: a flat fill gives both ends one colour")
{
	const LogoShape shape = ParseLogoSvg(Svg(R"svg(<path d="M10 10 L50 10 L50 40 Z" fill="#102030"/>)svg"));

	REQUIRE(shape.Faces.size() == 1);
	CHECK(shape.Faces[0].FromColour == IM_COL32(0x10, 0x20, 0x30, 0xff));
	CHECK(shape.Faces[0].ToColour == shape.Faces[0].FromColour);
}

TEST_CASE("LogoShape: a gradient in bounding-box units spans the face it fills and keeps only its first and last stops")
{
	const LogoShape shape = ParseLogoSvg(Svg(
		R"svg(<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">)svg"
		R"svg(<stop offset="0" stop-color="#000000"/><stop offset="0.5" stop-color="#808080"/><stop offset="1" stop-color="#ffffff"/>)svg"
		R"svg(</linearGradient></defs>)svg"
		R"svg(<path d="M10 20 L50 20 L50 60 Z" fill="url(#g)"/>)svg"));

	REQUIRE(shape.Faces.size() == 1);
	CHECK(shape.Faces[0].From.x == doctest::Approx(10.0f));
	CHECK(shape.Faces[0].From.y == doctest::Approx(20.0f));
	CHECK(shape.Faces[0].To.x == doctest::Approx(10.0f));
	CHECK(shape.Faces[0].To.y == doctest::Approx(60.0f));

	CHECK(shape.Faces[0].FromColour == IM_COL32(0x00, 0x00, 0x00, 0xff));
	CHECK(shape.Faces[0].ToColour == IM_COL32(0xff, 0xff, 0xff, 0xff));
}

TEST_CASE("LogoShape: a circle is background and left out")
{
	const LogoShape shape = ParseLogoSvg(
		Svg(R"svg(<circle cx="50" cy="50" r="50" fill="#000000"/><path d="M10 10 L50 10 L50 40 Z" fill="#ffffff"/>)svg"));

	REQUIRE(shape.Faces.size() == 1);
	CHECK(shape.Min.x == doctest::Approx(10.0f));
	CHECK(shape.Max.y == doctest::Approx(40.0f));
}

TEST_CASE("LogoShape: a curve, a relative command, a second outline, an unknown gradient or an odd coordinate empties the whole shape")
{
	const std::string validFace = R"svg(<path d="M10 10 L50 10 L50 40 Z" fill="#ffffff"/>)svg";

	CHECK(ParseLogoSvg(Svg(validFace + R"svg(<path d="M0 0 C1 1 2 2 3 3 Z" fill="#ffffff"/>)svg")).Faces.empty());
	CHECK(ParseLogoSvg(Svg(validFace + R"svg(<path d="M0 0 l10 0 l0 10 Z" fill="#ffffff"/>)svg")).Faces.empty());
	CHECK(ParseLogoSvg(Svg(validFace + R"svg(<path d="M0 0 L1 0 L1 1 Z M5 5 L6 5 L6 6 Z" fill="#ffffff"/>)svg")).Faces.empty());
	CHECK(ParseLogoSvg(Svg(validFace + R"svg(<path d="M0 0 L1 0 L1 1 Z" fill="url(#missing)"/>)svg")).Faces.empty());
	CHECK(ParseLogoSvg(Svg(validFace + R"svg(<path d="M0 0 L1 0 L1 Z" fill="#ffffff"/>)svg")).Faces.empty());
}

TEST_CASE("LogoShape: faces keep the order of the document, so a later face paints over an earlier one")
{
	const LogoShape shape = ParseLogoSvg(Svg(R"svg(<path d="M10 10 L50 10 L50 40 Z" fill="#111111"/>)svg"
											 R"svg(<path d="M20 20 L60 20 L60 50 Z" fill="#222222"/>)svg"));

	REQUIRE(shape.Faces.size() == 2);
	CHECK(shape.Faces[0].FromColour == IM_COL32(0x11, 0x11, 0x11, 0xff));
	CHECK(shape.Faces[1].FromColour == IM_COL32(0x22, 0x22, 0x22, 0xff));
}

TEST_CASE("LogoShape: text that is not XML gives no faces")
{
	CHECK(ParseLogoSvg("<svg><path").Faces.empty());
	CHECK(ParseLogoSvg("").Faces.empty());
}

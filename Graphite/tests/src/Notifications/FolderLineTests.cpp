#include <doctest/doctest.h>

#include "Notifications/FolderLine.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace {
	constexpr float kCharacterAdvance = 10.0f;
	constexpr std::string_view kEllipsis = "\u2026";
	constexpr std::string_view kAWithOgonek = "\u0105";

	bool StartsCharacter(char byte)
	{
		const unsigned char value = static_cast<unsigned char>(byte);
		const unsigned char continuationMask = 0xC0u;
		const unsigned char continuationBits = 0x80u;
		return (value & continuationMask) != continuationBits;
	}

	// A fixed advance per character rather than per byte, so a cut inside a character shows.
	float FixedAdvanceWidth(std::string_view text)
	{
		float width = 0.0f;
		for (const char byte : text)
		{
			if (StartsCharacter(byte))
			{
				width += kCharacterAdvance;
			}
		}
		return width;
	}

	std::string Repeated(std::string_view piece, int repeatCount)
	{
		std::string repeated;
		for (int pieceNumber = 1; pieceNumber <= repeatCount; ++pieceNumber)
		{
			repeated += piece;
		}
		return repeated;
	}
}

TEST_CASE("A folder that fits its column is shown whole")
{
	const std::string_view folder = "D:/Repos/IDE";
	const float columnWidth = 200.0f;

	const std::string line = Graphite::ElideFolderLine(folder, columnWidth, FixedAdvanceWidth);

	CHECK(line == std::string(folder));
}

TEST_CASE("A folder wider than its column becomes an ellipsis and the longest tail of it that fits")
{
	const std::string_view folder = "D:/Repos/Graphite/Projects/Notifications";
	const float columnWidth = 205.0f;
	const std::size_t tailThatFits = 19;

	const std::string line = Graphite::ElideFolderLine(folder, columnWidth, FixedAdvanceWidth);

	CHECK(line == std::string(kEllipsis) + std::string(folder.substr(folder.size() - tailThatFits)));
	CHECK(FixedAdvanceWidth(line) <= columnWidth);
	CHECK(line.ends_with("Notifications"));
}

TEST_CASE("A folder cut on the left keeps whole characters")
{
	const int lettersInTheName = 10;
	const std::string folder = "D:/" + Repeated(kAWithOgonek, lettersInTheName);
	const float columnWidth = 60.0f;
	const int lettersThatFitBesideTheEllipsis = 5;

	const std::string line = Graphite::ElideFolderLine(folder, columnWidth, FixedAdvanceWidth);

	CHECK(line == std::string(kEllipsis) + Repeated(kAWithOgonek, lettersThatFitBesideTheEllipsis));
}

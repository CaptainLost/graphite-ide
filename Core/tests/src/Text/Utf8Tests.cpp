#include <doctest/doctest.h>

#include "Core/Text/Utf8.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Core::AppendUtf8;
using Core::DecodeUtf8;
using Core::kReplacementCharacter;
using Core::Utf8Decoder;
using Core::Utf8Status;
using Core::Utf8Step;

namespace {
	struct DecodedCharacter
	{
		std::optional<char32_t> CodePoint;
		std::size_t Offset = 0;
	};

	DecodedCharacter DecodeFirst(std::string_view bytes)
	{
		std::size_t offset = 0;
		const std::optional<char32_t> codePoint = DecodeUtf8(bytes, offset);
		return DecodedCharacter{.CodePoint = codePoint, .Offset = offset};
	}

	std::u32string FeedAllReplacingBrokenSequences(std::string_view bytes)
	{
		Utf8Decoder decoder;
		std::u32string decoded;

		for (const char byte : bytes)
		{
			Utf8Step step = decoder.Feed(byte);

			if (step.Status == Utf8Status::Interrupted)
			{
				decoded.push_back(kReplacementCharacter);
				step = decoder.Feed(byte);
			}

			if (step.Status == Utf8Status::Decoded)
			{
				decoded.push_back(step.CodePoint);
			}
			else if (step.Status == Utf8Status::Invalid)
			{
				decoded.push_back(kReplacementCharacter);
			}
		}

		return decoded;
	}
}

TEST_CASE("DecodeUtf8 reads an ASCII byte as itself")
{
	const DecodedCharacter decoded = DecodeFirst("A");

	CHECK(decoded.CodePoint == U'A');
	CHECK(decoded.Offset == 1);
}

TEST_CASE("DecodeUtf8 reads two-, three- and four-byte sequences")
{
	const DecodedCharacter twoBytes = DecodeFirst("\xC5\x82");
	const DecodedCharacter threeBytes = DecodeFirst("\xE2\x82\xAC");
	const DecodedCharacter fourBytes = DecodeFirst("\xF0\x9F\x98\x80");

	CHECK(twoBytes.CodePoint == U'\u0142');
	CHECK(twoBytes.Offset == 2);
	CHECK(threeBytes.CodePoint == U'\u20AC');
	CHECK(threeBytes.Offset == 3);
	CHECK(fourBytes.CodePoint == U'\U0001F600');
	CHECK(fourBytes.Offset == 4);
}

TEST_CASE("DecodeUtf8 walks a string one character at a time")
{
	const std::string_view text = "a\xC5\x82\xE2\x82\xAC";
	std::size_t offset = 0;

	CHECK(DecodeUtf8(text, offset) == U'a');
	CHECK(DecodeUtf8(text, offset) == U'\u0142');
	CHECK(DecodeUtf8(text, offset) == U'\u20AC');
	CHECK(offset == text.size());
}

TEST_CASE("DecodeUtf8 rejects an overlong form such as a slash written in two bytes to slip past a naive path check")
{
	const DecodedCharacter overlongSlash = DecodeFirst("\xC0\xAF");

	CHECK_FALSE(overlongSlash.CodePoint);
	CHECK(overlongSlash.Offset == 1);
}

TEST_CASE("DecodeUtf8 rejects the overlong forms of every length")
{
	CHECK_FALSE(DecodeFirst("\xC1\xBF").CodePoint);
	CHECK_FALSE(DecodeFirst("\xE0\x9F\xBF").CodePoint);
	CHECK_FALSE(DecodeFirst("\xF0\x8F\xBF\xBF").CodePoint);
}

TEST_CASE("DecodeUtf8 rejects an encoded surrogate")
{
	const DecodedCharacter decoded = DecodeFirst("\xED\xA0\x80");

	CHECK_FALSE(decoded.CodePoint);
	CHECK(decoded.Offset == 1);
}

TEST_CASE("DecodeUtf8 rejects a value above U+10FFFF")
{
	CHECK_FALSE(DecodeFirst("\xF4\x90\x80\x80").CodePoint);
	CHECK_FALSE(DecodeFirst("\xF5\x80\x80\x80").CodePoint);
	CHECK_FALSE(DecodeFirst("\xF7\xBF\xBF\xBF").CodePoint);
}

TEST_CASE("DecodeUtf8 rejects a truncated sequence and consumes what it read")
{
	const DecodedCharacter decoded = DecodeFirst("\xE2\x82");

	CHECK_FALSE(decoded.CodePoint);
	CHECK(decoded.Offset == 2);
}

TEST_CASE("DecodeUtf8 rejects a stray continuation byte one byte at a time")
{
	const DecodedCharacter decoded = DecodeFirst("\x82!");

	CHECK_FALSE(decoded.CodePoint);
	CHECK(decoded.Offset == 1);
}

TEST_CASE("DecodeUtf8 stops a broken sequence before the byte that broke it")
{
	const std::string_view text = "\xC5Z";
	std::size_t offset = 0;

	CHECK_FALSE(DecodeUtf8(text, offset));
	CHECK(offset == 1);
	CHECK(DecodeUtf8(text, offset) == U'Z');
}

TEST_CASE("DecodeUtf8 accepts the edges of the valid ranges")
{
	CHECK(DecodeFirst("\xC2\x80").CodePoint == U'\u0080');
	CHECK(DecodeFirst("\xED\x9F\xBF").CodePoint == U'\uD7FF');
	CHECK(DecodeFirst("\xEE\x80\x80").CodePoint == U'\uE000');
	CHECK(DecodeFirst("\xEF\xBF\xBF").CodePoint == U'\uFFFF');
	CHECK(DecodeFirst("\xF4\x8F\xBF\xBF").CodePoint == U'\U0010FFFF');
}

TEST_CASE("AppendUtf8 round-trips the boundary code points")
{
	const std::vector<char32_t> codePoints = {
		U'\0', U'\u007F', U'\u0080', U'\u07FF', U'\u0800', U'\uD7FF',
		U'\uE000', U'\uFFFF', U'\U00010000', U'\U0010FFFF'};

	for (const char32_t codePoint : codePoints)
	{
		std::string text;
		AppendUtf8(text, codePoint);

		std::size_t offset = 0;
		CHECK(DecodeUtf8(text, offset) == codePoint);
		CHECK(offset == text.size());
	}
}

TEST_CASE("AppendUtf8 writes each length's exact bytes")
{
	std::string text;
	AppendUtf8(text, U'A');
	AppendUtf8(text, U'\u0142');
	AppendUtf8(text, U'\u20AC');
	AppendUtf8(text, U'\U0001F600');

	CHECK(text == "A\xC5\x82\xE2\x82\xAC\xF0\x9F\x98\x80");
}

TEST_CASE("AppendUtf8 writes a replacement for what is not a character")
{
	constexpr char32_t kFirstSurrogate = 0xD800;
	constexpr char32_t kLastSurrogate = 0xDFFF;
	constexpr char32_t kFirstValueAboveLastCodePoint = 0x110000;

	std::string text;
	AppendUtf8(text, kFirstSurrogate);
	AppendUtf8(text, kFirstValueAboveLastCodePoint);

	CHECK(text == "\xEF\xBF\xBD\xEF\xBF\xBD");

	std::string lastSurrogateText;
	AppendUtf8(lastSurrogateText, kLastSurrogate);

	CHECK(lastSurrogateText == "\xEF\xBF\xBD");
}

TEST_CASE("Utf8Decoder decodes a character fed one byte at a time")
{
	Utf8Decoder decoder;

	CHECK(decoder.Feed('\xE2').Status == Utf8Status::Pending);
	CHECK(decoder.IsPending());
	CHECK(decoder.Feed('\x82').Status == Utf8Status::Pending);

	const Utf8Step step = decoder.Feed('\xAC');

	CHECK(step.Status == Utf8Status::Decoded);
	CHECK(step.CodePoint == U'\u20AC');
	CHECK_FALSE(decoder.IsPending());
}

TEST_CASE("Utf8Decoder applies the same rules as DecodeUtf8")
{
	CHECK(FeedAllReplacingBrokenSequences("\xC0\xAF") == U"\uFFFD\uFFFD");
	CHECK(FeedAllReplacingBrokenSequences("\xED\xA0\x80") == U"\uFFFD\uFFFD\uFFFD");
	CHECK(FeedAllReplacingBrokenSequences("\xF4\x90\x80\x80") == U"\uFFFD\uFFFD\uFFFD\uFFFD");
	CHECK(FeedAllReplacingBrokenSequences("a\x82!") == U"a\uFFFD!");
	CHECK(FeedAllReplacingBrokenSequences("\xC5Z") == U"\uFFFDZ");
	CHECK(FeedAllReplacingBrokenSequences("\xF0\x9F\x98\x80") == U"\U0001F600");
}

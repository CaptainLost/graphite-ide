#include <doctest/doctest.h>

#include "Core/Text/Utf8.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Core::AppendUtf8;
using Core::DecodeUtf8;
using Core::Utf8Decoder;
using Core::Utf8Status;

namespace {
	struct Decoded
	{
		std::optional<char32_t> CodePoint;
		std::size_t Offset = 0;
	};

	Decoded DecodeFirst(std::string_view bytes)
	{
		std::size_t offset = 0;
		const auto codePoint = DecodeUtf8(bytes, offset);
		return {codePoint, offset};
	}

	// Feeds the whole stream through the incremental decoder the way the terminal does:
	// a broken sequence becomes one U+FFFD, and the byte that broke it is read again.
	std::u32string FeedAll(std::string_view bytes)
	{
		Utf8Decoder decoder;
		std::u32string out;

		for (const char byte : bytes)
		{
			auto step = decoder.Feed(byte);

			if (step.Status == Utf8Status::Interrupted)
			{
				out.push_back(U'\uFFFD');
				step = decoder.Feed(byte);
			}

			if (step.Status == Utf8Status::Decoded)
			{
				out.push_back(step.CodePoint);
			}
			else if (step.Status == Utf8Status::Invalid)
			{
				out.push_back(U'\uFFFD');
			}
		}

		return out;
	}
}

TEST_CASE("DecodeUtf8 reads an ASCII byte as itself")
{
	const auto decoded = DecodeFirst("A");

	CHECK(decoded.CodePoint == U'A');
	CHECK(decoded.Offset == 1);
}

TEST_CASE("DecodeUtf8 reads two-, three- and four-byte sequences")
{
	const auto two = DecodeFirst("\xC5\x82");		   // U+0142
	const auto three = DecodeFirst("\xE2\x82\xAC");	   // U+20AC
	const auto four = DecodeFirst("\xF0\x9F\x98\x80"); // U+1F600

	CHECK(two.CodePoint == U'\u0142');
	CHECK(two.Offset == 2);
	CHECK(three.CodePoint == U'\u20AC');
	CHECK(three.Offset == 3);
	CHECK(four.CodePoint == U'\U0001F600');
	CHECK(four.Offset == 4);
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

TEST_CASE("DecodeUtf8 rejects an overlong form")
{
	// C0 AF is '/' in two bytes, the form that gets smuggled past a naive path check.
	const auto decoded = DecodeFirst("\xC0\xAF");

	CHECK_FALSE(decoded.CodePoint);
	CHECK(decoded.Offset == 1);
}

TEST_CASE("DecodeUtf8 rejects the overlong forms of every length")
{
	CHECK_FALSE(DecodeFirst("\xC1\xBF").CodePoint);
	CHECK_FALSE(DecodeFirst("\xE0\x9F\xBF").CodePoint);
	CHECK_FALSE(DecodeFirst("\xF0\x8F\xBF\xBF").CodePoint);
}

TEST_CASE("DecodeUtf8 rejects an encoded surrogate")
{
	const auto decoded = DecodeFirst("\xED\xA0\x80");

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
	const auto decoded = DecodeFirst("\xE2\x82");

	CHECK_FALSE(decoded.CodePoint);
	CHECK(decoded.Offset == 2);
}

TEST_CASE("DecodeUtf8 rejects a stray continuation byte one byte at a time")
{
	const auto decoded = DecodeFirst("\x82!");

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
	std::string text;
	AppendUtf8(text, static_cast<char32_t>(0xD800));
	AppendUtf8(text, static_cast<char32_t>(0x110000));

	CHECK(text == "\xEF\xBF\xBD\xEF\xBF\xBD");
}

TEST_CASE("Utf8Decoder decodes a character fed one byte at a time")
{
	Utf8Decoder decoder;

	CHECK(decoder.Feed('\xE2').Status == Utf8Status::Pending);
	CHECK(decoder.IsPending());
	CHECK(decoder.Feed('\x82').Status == Utf8Status::Pending);

	const auto step = decoder.Feed('\xAC');

	CHECK(step.Status == Utf8Status::Decoded);
	CHECK(step.CodePoint == U'\u20AC');
	CHECK_FALSE(decoder.IsPending());
}

TEST_CASE("Utf8Decoder applies the same rules as DecodeUtf8")
{
	CHECK(FeedAll("\xC0\xAF") == U"\uFFFD\uFFFD");
	CHECK(FeedAll("\xED\xA0\x80") == U"\uFFFD\uFFFD\uFFFD");
	CHECK(FeedAll("\xF4\x90\x80\x80") == U"\uFFFD\uFFFD\uFFFD\uFFFD");
	CHECK(FeedAll("a\x82!") == U"a\uFFFD!");
	CHECK(FeedAll("\xC5Z") == U"\uFFFDZ");
	CHECK(FeedAll("\xF0\x9F\x98\x80") == U"\U0001F600");
}

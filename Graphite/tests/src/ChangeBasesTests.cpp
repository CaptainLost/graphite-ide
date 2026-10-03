#include <doctest/doctest.h>

#include "Git/ChangeBases.h"

#include "Core/Git/Repository.h"

#include <string>

using Graphite::DecodeBaseText;
using Graphite::MakeChangeBases;

TEST_CASE("Plain UTF-8 text is taken as it is")
{
	CHECK(DecodeBaseText("one\ntwo\n") == std::optional<std::string>{"one\ntwo\n"});
}

TEST_CASE("Windows line endings are read as the buffer holds them")
{
	CHECK(DecodeBaseText("one\r\ntwo\r\n") == std::optional<std::string>{"one\ntwo\n"});
}

TEST_CASE("A UTF-8 byte order mark is not part of the text")
{
	CHECK(DecodeBaseText("\xEF\xBB\xBFone\n") == std::optional<std::string>{"one\n"});
}

TEST_CASE("UTF-16 text is decoded to UTF-8")
{
	const std::string bytes{"\xFF\xFE"
							"a\0\n\0",
							6};
	CHECK(DecodeBaseText(bytes) == std::optional<std::string>{"a\n"});
}

TEST_CASE("Binary content is not text to diff")
{
	const std::string bytes{"\x7F"
							"ELF\0\0\0\x01\x02\0\0",
							11};
	CHECK_FALSE(DecodeBaseText(bytes).has_value());
}

TEST_CASE("Content past the size limit is not diffed")
{
	const std::string bytes(static_cast<std::size_t>(Core::MaximumDiffTextSize) + 1, 'a');
	CHECK_FALSE(DecodeBaseText(bytes).has_value());
}

TEST_CASE("A file in neither HEAD nor the index is tracked with no sides")
{
	const auto bases = MakeChangeBases(std::nullopt, std::nullopt);

	CHECK(bases.Tracked);
	CHECK_FALSE(bases.Unusable);
	CHECK_FALSE(bases.Head.has_value());
	CHECK_FALSE(bases.Index.has_value());
}

TEST_CASE("A side that is not text makes the file unusable")
{
	const auto bases = MakeChangeBases(std::string{"one\n"}, std::string{"a\0b", 3});

	CHECK(bases.Tracked);
	CHECK(bases.Unusable);
}

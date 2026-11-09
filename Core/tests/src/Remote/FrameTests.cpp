#include <doctest/doctest.h>

#include "Core/Remote/Frame.h"

#include <optional>
#include <string>

using Core::EncodeFrame;
using Core::Frame;
using Core::FrameReader;

TEST_CASE("A frame comes back as the header and payload it was made of")
{
	FrameReader reader;
	reader.Feed(EncodeFrame(R"({"id":1})", std::string("bytes\0with a nul", 16)));

	const std::optional<Frame> frame = reader.Take();

	REQUIRE(frame.has_value());
	CHECK(frame->Header == R"({"id":1})");
	CHECK(frame->Payload == std::string("bytes\0with a nul", 16));
	CHECK_FALSE(reader.Take().has_value());
}

TEST_CASE("A frame with no payload comes back with an empty one")
{
	FrameReader reader;
	reader.Feed(EncodeFrame("{}", ""));

	const std::optional<Frame> frame = reader.Take();

	REQUIRE(frame.has_value());
	CHECK(frame->Payload.empty());
}

TEST_CASE("A frame fed a byte at a time is taken once, only after its last byte")
{
	const std::string encoded = EncodeFrame(R"({"event":"changes"})", "payload");
	FrameReader reader;

	for (std::size_t index = 0; index + 1 < encoded.size(); ++index)
	{
		reader.Feed(encoded.substr(index, 1));
		REQUIRE_FALSE(reader.Take().has_value());
	}

	reader.Feed(encoded.substr(encoded.size() - 1));

	const std::optional<Frame> frame = reader.Take();
	REQUIRE(frame.has_value());
	CHECK(frame->Payload == "payload");
	CHECK_FALSE(reader.Take().has_value());
}

TEST_CASE("Two frames in one read are taken in order")
{
	FrameReader reader;
	reader.Feed(EncodeFrame("1", "a") + EncodeFrame("2", "b"));

	const std::optional<Frame> first = reader.Take();
	const std::optional<Frame> second = reader.Take();

	REQUIRE(first.has_value());
	REQUIRE(second.has_value());
	CHECK(first->Header == "1");
	CHECK(second->Header == "2");
	CHECK_FALSE(reader.Take().has_value());
}

TEST_CASE("A length past the limit fails the reader and yields nothing")
{
	FrameReader reader;
	reader.Feed(std::string("\xFF\xFF\xFF\xFF", 4));

	CHECK_FALSE(reader.Take().has_value());
	CHECK(reader.HasFailed());

	reader.Feed(EncodeFrame("{}", ""));
	CHECK_FALSE(reader.Take().has_value());
}

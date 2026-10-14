#include <doctest/doctest.h>

#include "Language/Server/MessageStream.h"

TEST_CASE("MessageStream yields nothing until a whole message has arrived")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: 7\r\n\r\n{\"a\":1");
	CHECK_FALSE(stream.Take().has_value());

	stream.Append("}");
	CHECK(stream.Take() == "{\"a\":1}");
}

TEST_CASE("MessageStream yields both messages when two arrive in one chunk")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: 2\r\n\r\n{}Content-Length: 4\r\n\r\n[1,2");

	CHECK(stream.Take() == "{}");
	CHECK(stream.Take() == "[1,2");
	CHECK_FALSE(stream.Take().has_value());
}

TEST_CASE("MessageStream splits a header across appends")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Le");
	CHECK_FALSE(stream.Take().has_value());

	stream.Append("ngth: 2\r\n\r\n{}");
	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream ignores headers it does not need")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Type: application/vscode-jsonrpc\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream drops a header with no length rather than stalling")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Bogus: 1\r\n\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream drops a length that overflows rather than stalling")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: 99999999999999999999\r\n\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream drops a negative length rather than stalling")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: -5\r\n\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream drops a length that is not a number rather than stalling")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: abc\r\n\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream drops a length followed by something other than a number rather than reading its digits")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: 2x\r\n\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream accepts trailing spaces after a length")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: 2  \r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

TEST_CASE("MessageStream drops a length larger than any message rather than waiting for it")
{
	Graphite::MessageStream stream;

	stream.Append("Content-Length: 99999999999\r\n\r\nContent-Length: 2\r\n\r\n{}");

	CHECK(stream.Take() == "{}");
}

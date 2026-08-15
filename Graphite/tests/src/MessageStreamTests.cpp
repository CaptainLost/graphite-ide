#include <doctest/doctest.h>

#include "Lang/Server/MessageStream.h"

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

#include <doctest/doctest.h>

#include "Lang/Server/JsonRpc.h"

TEST_CASE("JsonRpc frames a request with an increasing id")
{
	Graphite::JsonRpc rpc;

	const auto [first, firstBytes] = rpc.Request("textDocument/definition", {});
	const auto [second, secondBytes] = rpc.Request("textDocument/references", {});

	CHECK(first != second);
	CHECK(firstBytes.find("Content-Length: ") == 0);
	CHECK(firstBytes.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
	CHECK(rpc.IsOutstanding(first));
}

TEST_CASE("JsonRpc pairs a reply with its request and forgets it afterwards")
{
	Graphite::JsonRpc rpc;

	const auto [id, bytes] = rpc.Request("textDocument/definition", {});
	const auto message = rpc.Receive(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
									 R"(,"result":[{"uri":"file:///a.cpp"}]})");

	REQUIRE(message.has_value());
	CHECK(message->ReplyTo == id);
	CHECK_FALSE(message->IsError);
	CHECK(message->Payload.size() == 1);
	CHECK_FALSE(rpc.IsOutstanding(id));
}

TEST_CASE("JsonRpc accepts replies in an order it did not ask in")
{
	Graphite::JsonRpc rpc;

	const auto [first, firstBytes] = rpc.Request("a", {});
	const auto [second, secondBytes] = rpc.Request("b", {});

	CHECK(rpc.Receive(R"({"id":)" + std::to_string(second) + R"(,"result":2})")->ReplyTo == second);
	CHECK(rpc.Receive(R"({"id":)" + std::to_string(first) + R"(,"result":1})")->ReplyTo == first);
}

TEST_CASE("JsonRpc reports an error reply as one")
{
	Graphite::JsonRpc rpc;

	const auto [id, bytes] = rpc.Request("a", {});
	const auto message = rpc.Receive(R"({"id":)" + std::to_string(id) +
									 R"(,"error":{"code":-32601,"message":"no"}})");

	REQUIRE(message.has_value());
	CHECK(message->IsError);
	CHECK_FALSE(rpc.IsOutstanding(id));
}

TEST_CASE("JsonRpc passes a server-initiated notification through with its method")
{
	Graphite::JsonRpc rpc;

	const auto message = rpc.Receive(R"({"method":"$/progress","params":{"value":42}})");

	REQUIRE(message.has_value());
	CHECK(message->Method == "$/progress");
	CHECK_FALSE(message->ReplyTo.has_value());
	CHECK(message->Payload["value"] == 42);
}

TEST_CASE("JsonRpc ignores a reply to an id it never asked about")
{
	Graphite::JsonRpc rpc;

	CHECK_FALSE(rpc.Receive(R"({"id":9999,"result":1})").has_value());
}

TEST_CASE("JsonRpc ignores a body that is not JSON rather than throwing")
{
	Graphite::JsonRpc rpc;

	CHECK_FALSE(rpc.Receive("not json at all").has_value());
}

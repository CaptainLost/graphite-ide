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

TEST_CASE("JsonRpc ignores a reply whose id is a string rather than throwing")
{
	Graphite::JsonRpc rpc;

	CHECK_FALSE(rpc.Receive(R"({"id":"not-a-number","result":1})").has_value());
}

TEST_CASE("JsonRpc frames a reply carrying the id the server asked under")
{
	Graphite::JsonRpc rpc;

	const std::string bytes = rpc.Reply(7, nlohmann::json::array({nlohmann::json::object()}));

	CHECK(bytes.find("Content-Length: ") == 0);
	CHECK(bytes.find(R"("id":7)") != std::string::npos);
	CHECK(bytes.find(R"("result":[{}])") != std::string::npos);
	CHECK(bytes.find("error") == std::string::npos);
}

TEST_CASE("JsonRpc frames an error reply so a server is never left waiting")
{
	Graphite::JsonRpc rpc;

	const std::string bytes = rpc.ErrorReply(9, -32601, "method not found");

	CHECK(bytes.find(R"("id":9)") != std::string::npos);
	CHECK(bytes.find(R"("code":-32601)") != std::string::npos);
	CHECK(bytes.find("method not found") != std::string::npos);
	CHECK(bytes.find(R"("result")") == std::string::npos);
}

TEST_CASE("JsonRpc does not record a reply's id as one it is waiting on")
{
	Graphite::JsonRpc rpc;

	rpc.Reply(7, nullptr);

	CHECK_FALSE(rpc.IsOutstanding(7));
}

TEST_CASE("JsonRpc reads a server-initiated request as one, not as a reply")
{
	Graphite::JsonRpc rpc;

	const auto message = rpc.Receive(R"({"id":7,"method":"workspace/configuration","params":{}})");

	REQUIRE(message.has_value());
	CHECK(message->Method == "workspace/configuration");
	CHECK(message->RespondTo == 7);
	CHECK_FALSE(message->ReplyTo.has_value());
}

TEST_CASE("JsonRpc keeps a server-initiated request's id exactly as it arrived")
{
	Graphite::JsonRpc rpc;

	const auto named = rpc.Receive(R"({"id":"abc","method":"workspace/configuration","params":{}})");

	REQUIRE(named.has_value());
	REQUIRE(named->RespondTo.has_value());
	CHECK(*named->RespondTo == nlohmann::json("abc"));

	const auto numbered = rpc.Receive(R"({"id":7,"method":"workspace/configuration","params":{}})");

	REQUIRE(numbered.has_value());
	REQUIRE(numbered->RespondTo.has_value());
	CHECK(*numbered->RespondTo == nlohmann::json(7));
}

TEST_CASE("JsonRpc echoes a string id unchanged in a reply and an error reply")
{
	Graphite::JsonRpc rpc;

	CHECK(rpc.Reply(nlohmann::json("abc"), nullptr).find(R"("id":"abc")") != std::string::npos);
	CHECK(rpc.ErrorReply(nlohmann::json("abc"), -32601, "no").find(R"("id":"abc")") != std::string::npos);
}

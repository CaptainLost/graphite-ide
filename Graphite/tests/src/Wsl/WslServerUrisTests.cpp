#include <doctest/doctest.h>

#include "Wsl/WslServerUris.h"

#include <nlohmann/json.hpp>

#include <string>

using nlohmann::json;

namespace {

	const Core::WslSharePath kRoot{"Ubuntu", "/home/me/p"};

	json Sent(const json& message)
	{
		return json::parse(Graphite::ToDistroBody(message.dump(), kRoot));
	}

	json Received(const json& message)
	{
		return json::parse(Graphite::FromDistroBody(message.dump(), "Ubuntu"));
	}
}

TEST_CASE("A document opened in a WSL folder reaches its server by its Linux URI")
{
	const json opened = Sent({{"method", "textDocument/didOpen"},
							  {"params", {{"textDocument", {{"uri", "file:////wsl.localhost/Ubuntu/home/me/p/src/a.cpp"}, {"text", "int a;"}}}}}});

	CHECK(opened.at("params").at("textDocument").at("uri") == "file:///home/me/p/src/a.cpp");
	CHECK(opened.at("params").at("textDocument").at("text") == "int a;");
}

TEST_CASE("A WSL folder's server is initialised with the folder's Linux URI and path")
{
	const json initialise = Sent({{"method", "initialize"},
								  {"params", {{"rootUri", "file:////wsl.localhost/Ubuntu/home/me/p"}, {"rootPath", "\\\\wsl.localhost\\Ubuntu\\home\\me\\p"}}}});

	CHECK(initialise.at("params").at("rootUri") == "file:///home/me/p");
	CHECK(initialise.at("params").at("rootPath") == "/home/me/p");
}

TEST_CASE("A location a WSL folder's server answers with comes back as the share URI Graphite names the file by")
{
	const json answer = Received({{"id", 3}, {"result", json::array({{{"targetUri", "file:///home/me/p/include/b.h"}, {"targetRange", json::object()}}})}});

	CHECK(answer.at("result").at(0).at("targetUri") == "file:////wsl.localhost/Ubuntu/home/me/p/include/b.h");
}

TEST_CASE("A message without a file URI passes to and from a WSL server byte for byte")
{
	const std::string tokens = R"({"id":7,"result":{"data":[0,4,3,1,0]}})";

	CHECK(Graphite::ToDistroBody(tokens, kRoot) == tokens);
	CHECK(Graphite::FromDistroBody(tokens, "Ubuntu") == tokens);
}

TEST_CASE("Only the WSL folder's own distro is translated: another distro, a Windows file and other schemes stay as they are")
{
	const json sent = Sent({{"params",
							 {{"a", "file:////wsl.localhost/Debian/home/me/p/x.cpp"},
							  {"b", "file:///C:/work/y.cpp"},
							  {"c", "https://example.com/file:///z"}}}});

	CHECK(sent.at("params").at("a") == "file:////wsl.localhost/Debian/home/me/p/x.cpp");
	CHECK(sent.at("params").at("b") == "file:///C:/work/y.cpp");
	CHECK(sent.at("params").at("c") == "https://example.com/file:///z");

	// A Windows server URI never comes from a Linux server; one that does is not made into a share path.
	const json received = Received({{"uri", "file:///C:/work/y.cpp"}});
	CHECK(received.at("uri") == "file:///C:/work/y.cpp");
}

TEST_CASE("A Linux name holding a backslash stays one name both ways")
{
	// The share spells a '\' in a Linux name as U+F05C.
	const json sent = Sent({{"uri", "file:////wsl.localhost/Ubuntu/home/me/p/a%EF%81%9Cb.cpp"}});
	CHECK(sent.at("uri") == "file:///home/me/p/a%5Cb.cpp");

	const json received = Received({{"uri", "file:///home/me/p/a%5Cb.cpp"}});
	CHECK(received.at("uri") == "file:////wsl.localhost/Ubuntu/home/me/p/a%EF%81%9Cb.cpp");
}

TEST_CASE("A top-level Linux name with a colon is not taken for a drive")
{
	const json received = Received({{"uri", "file:///a:b"}});

	CHECK(received.at("uri") == "file:////wsl.localhost/Ubuntu/a%EF%80%BAb");
}

TEST_CASE("Document text that happens to start with file: reaches a WSL server unchanged")
{
	const json sent = Sent({{"params", {{"contentChanges", json::array({{{"text", "file:////wsl.localhost/Ubuntu/home/me/p/a.cpp\nint a;\n"}}})}}}});

	CHECK(sent.at("params").at("contentChanges").at(0).at("text") == "file:////wsl.localhost/Ubuntu/home/me/p/a.cpp\nint a;\n");
}

TEST_CASE("A document named with the distro in another case still reaches the server by its Linux URI")
{
	const json sent = Sent({{"uri", "file:////wsl.localhost/ubuntu/home/me/p/a.cpp"}});

	CHECK(sent.at("uri") == "file:///home/me/p/a.cpp");
}

TEST_CASE("A Linux name with a space, a percent sign or a character Windows forbids round-trips between the share and the distro")
{
	// The share spells a ':' in a Linux name as U+F03A; the distro's URI has the ':' itself.
	const json sent = Sent({{"uri", "file:////wsl.localhost/Ubuntu/home/me/p/my%20dir/a%EF%80%BAb%25.cpp"}});
	CHECK(sent.at("uri") == "file:///home/me/p/my%20dir/a:b%25.cpp");

	const json received = Received({{"uri", "file:///home/me/p/my%20dir/a%3Ab%25.cpp"}});
	CHECK(received.at("uri") == "file:////wsl.localhost/Ubuntu/home/me/p/my%20dir/a%EF%80%BAb%25.cpp");
}

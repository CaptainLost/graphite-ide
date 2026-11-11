#include <doctest/doctest.h>

#include "Language/Server/LspClient.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerLaunch.h"
#include "Wsl/WslServerUris.h"

#include "LspMessages.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

	const std::filesystem::path kRoot{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	const std::filesystem::path kSource{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\a.cpp"};

	// A clangd launch whose messages are spoken in the distro's paths, with no process behind it.
	Graphite::LspClient DistroClient()
	{
		Graphite::ServerLaunch launch = Graphite::LaunchFor(*Graphite::FindServer("clangd"), "clangd", Graphite::ServerOverrides{});
		const Core::WslSharePath root{"Ubuntu", "/home/me/p"};
		launch.Translation = Graphite::BodyTranslation{.ToServer = [root](std::string_view body) { return Graphite::ToDistroBody(body, root); },
													   .FromServer = [](std::string_view body) { return Graphite::FromDistroBody(body, "Ubuntu"); }};
		return Graphite::LspClient{launch, kRoot, Graphite::LspClient::WithoutServer{}};
	}
}

TEST_CASE("A language server in a WSL distro is initialised with the folder's Linux URI and sent documents by theirs")
{
	Graphite::LspClient client = DistroClient();
	client.Deliver(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
	client.OpenDocument(kSource, "int a;\n");

	const std::vector<std::string> sent = client.TakeSent();
	const std::vector<nlohmann::json> initialise = MessagesFor(sent, "initialize");
	REQUIRE(initialise.size() == 1);
	CHECK(initialise[0].at("params").at("rootUri") == "file:///home/me/p");

	const std::vector<nlohmann::json> opened = MessagesFor(sent, "textDocument/didOpen");
	REQUIRE(opened.size() == 1);
	CHECK(opened[0].at("params").at("textDocument").at("uri") == "file:///home/me/p/a.cpp");

	// Each message still carries the length of what it now holds.
	for (const std::string& framed : sent)
	{
		const std::size_t bodyStart = framed.find("\r\n\r\n") + 4;
		CHECK(framed.starts_with("Content-Length: " + std::to_string(framed.size() - bodyStart) + "\r\n\r\n"));
	}
}

TEST_CASE("Diagnostics a language server in a WSL distro publishes by a Linux URI land on the document's share path")
{
	Graphite::LspClient client = DistroClient();
	client.OpenDocument(kSource, "int a;\n");

	client.Deliver(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{
		"uri":"file:///home/me/p/a.cpp","version":1,
		"diagnostics":[{"range":{"start":{"line":0,"character":4},
			"end":{"line":0,"character":5}},"severity":1,"message":"boom"}]}})");

	Graphite::LspClient::Drained drained;
	client.Drain(drained);

	REQUIRE(drained.Diagnostics.size() == 1);
	CHECK(drained.Diagnostics[0].Path == kSource);
	REQUIRE(drained.Diagnostics[0].Items.size() == 1);
	CHECK(drained.Diagnostics[0].Items[0].Message == "boom");
}

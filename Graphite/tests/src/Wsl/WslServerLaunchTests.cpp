#include <doctest/doctest.h>

#include "Wsl/WslServerLaunch.h"

#include "Language/Server/LspClient.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Remote/AgentDeployment.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <thread>
#include <vector>

namespace {

	const std::filesystem::path kWsl{L"C:\\Windows\\System32\\wsl.exe"};
	const Core::WslSharePath kRoot{"Ubuntu", "/home/me/p"};

	const Graphite::ServerDefinition& Clangd()
	{
		return *Graphite::FindServer("clangd");
	}

	// A language server in Python: answers initialize, and publishes one diagnostic for each opened
	// document whose message names the URI it was opened by (after words, so it is not a URI itself).
	constexpr std::string_view kEchoServer = R"PY(import sys, json

def read():
    length = 0
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            sys.exit(0)
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b'content-length:'):
            length = int(line.split(b':')[1])
    return json.loads(sys.stdin.buffer.read(length))

def send(message):
    body = json.dumps(message).encode()
    sys.stdout.buffer.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
    sys.stdout.buffer.flush()

while True:
    message = read()
    method = message.get('method')
    if method == 'initialize':
        send({'jsonrpc': '2.0', 'id': message['id'], 'result': {'capabilities': {}}})
    elif method == 'textDocument/didOpen':
        document = message['params']['textDocument']
        send({'jsonrpc': '2.0', 'method': 'textDocument/publishDiagnostics', 'params': {'uri': document['uri'], 'version': document['version'],
              'diagnostics': [{'range': {'start': {'line': 0, 'character': 0}, 'end': {'line': 0, 'character': 1}}, 'severity': 1, 'message': 'opened as ' + document['uri']}]}})
    elif method == 'shutdown':
        send({'jsonrpc': '2.0', 'id': message['id'], 'result': None})
    elif method == 'exit':
        sys.exit(0)
)PY";

	bool HasUbuntu()
	{
		std::error_code error;
		return Core::IsWslAvailable() && std::filesystem::exists(L"\\\\wsl.localhost\\Ubuntu\\tmp", error);
	}
}

TEST_CASE("A WSL folder's language server is the configured Linux program, or the catalogue's name found on the distro's PATH")
{
	CHECK(Graphite::DistroServerProgram(Clangd(), "") == "clangd");
	CHECK(Graphite::DistroServerProgram(Clangd(), "/opt/llvm/bin/clangd") == "/opt/llvm/bin/clangd");
	// A Windows program cannot run in the distro: the configured path is for Windows folders.
	CHECK(Graphite::DistroServerProgram(Clangd(), L"C:\\llvm\\bin\\clangd.exe") == "clangd");
}

TEST_CASE("A WSL folder's language server starts in the distro through its login shell, in the folder, speaking Linux paths")
{
	const Graphite::ServerLaunch launch = Graphite::WslServerLaunch(Clangd(), "clangd", Graphite::ServerOverrides{.ExtraArguments = {"--log=error"}}, kRoot, kWsl);

	CHECK(launch.Binary == kWsl);
	const std::vector<std::string> expected{"-d", "Ubuntu", "--cd", "/home/me/p", "--exec", "/bin/sh", "-lc", "exec \"$0\" \"$@\"", "clangd",
											"--background-index", "--pch-storage=disk", "--log=error"};
	CHECK(launch.Arguments == expected);
	// Graphite's TMP, TEMP and DOTNET_ROOT are Windows paths the distro cannot use.
	CHECK(launch.Environment.empty());
	CHECK(launch.Translation.ToServer);
	CHECK(launch.Translation.FromServer);
	CHECK(launch.WhenProgramMissing == "clangd is not installed in Ubuntu, or not on its PATH");
	CHECK(launch.Description == "clangd in Ubuntu");
}

TEST_CASE("A server whose shell exits with 127 is reported missing, as its launch words it")
{
	Graphite::ServerLaunch launch = Graphite::LaunchFor(Clangd(), Core::FindOnPath("cmd.exe"), Graphite::ServerOverrides{});
	launch.Arguments = {"/c", "exit", "127"};
	launch.WhenProgramMissing = "clangd is not installed in Ubuntu, or not on its PATH";
	Graphite::LspClient client{launch, std::filesystem::current_path()};

	Graphite::ServerStatus status;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

	while (std::chrono::steady_clock::now() < deadline)
	{
		Graphite::LspClient::Drained drained;
		client.Drain(drained);
		status = drained.Status;

		if (status.State != Graphite::ServerState::Starting)
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{20});
	}

	CHECK(status.State == Graphite::ServerState::NotFound);
	CHECK(status.Detail == "clangd is not installed in Ubuntu, or not on its PATH");
}

TEST_CASE("A server that stops with another exit code is reported stopped, not missing")
{
	Graphite::ServerLaunch launch = Graphite::LaunchFor(Clangd(), Core::FindOnPath("cmd.exe"), Graphite::ServerOverrides{});
	launch.Arguments = {"/c", "exit", "1"};
	launch.WhenProgramMissing = "clangd is not installed in Ubuntu, or not on its PATH";
	Graphite::LspClient client{launch, std::filesystem::current_path()};

	Graphite::ServerStatus status;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

	while (std::chrono::steady_clock::now() < deadline)
	{
		Graphite::LspClient::Drained drained;
		client.Drain(drained);
		status = drained.Status;

		if (status.State != Graphite::ServerState::Starting)
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{20});
	}

	CHECK(status.State == Graphite::ServerState::Failed);
}

// The real distro: a Python server opened by the launch is spoken to in Linux URIs, and what it
// publishes lands on the share path.
TEST_CASE("A language server started in the real distro sees Linux URIs and its diagnostics land on the share path")
{
	if (!HasUbuntu())
	{
		MESSAGE("no WSL Ubuntu here: skipped");
		return;
	}

	std::error_code error;
	const std::string name = "graphite-wslserver-" + std::to_string(_getpid());
	const std::filesystem::path folder = std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\tmp\\"} / Core::FromUtf8(name);
	std::filesystem::create_directories(folder, error);
	std::ofstream{folder / "echo.py", std::ios::binary} << kEchoServer;
	const std::filesystem::path source = folder / "a.cpp";
	std::ofstream{source, std::ios::binary} << "int a;\n";

	const Core::WslSharePath root{"Ubuntu", "/tmp/" + name};
	Graphite::ServerLaunch launch =
		Graphite::WslServerLaunch(Clangd(), "/usr/bin/python3", Graphite::ServerOverrides{}, root, Core::WslExecutable());
	// python3 echo.py, then the catalogue's clangd arguments, which the script ignores.
	launch.Arguments.insert(launch.Arguments.begin() + 9, "echo.py");

	Graphite::LspClient client{launch, folder};
	client.OpenDocument(source, "int a;\n");

	std::vector<Graphite::PublishedDiagnostics> published;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};

	while (published.empty() && std::chrono::steady_clock::now() < deadline)
	{
		Graphite::LspClient::Drained drained;
		client.Drain(drained);
		published = std::move(drained.Diagnostics);
		std::this_thread::sleep_for(std::chrono::milliseconds{50});
	}

	REQUIRE(published.size() == 1);
	CHECK(published[0].Path == source);
	REQUIRE(published[0].Items.size() == 1);
	CHECK(published[0].Items[0].Message == "opened as file:///tmp/" + name + "/a.cpp");

	client.ShutDown(Graphite::LspClient::kShutdownGrace);
	std::filesystem::remove_all(folder, error);
}

TEST_CASE("A language server missing from the real distro says so, not that it stopped")
{
	if (!HasUbuntu())
	{
		MESSAGE("no WSL Ubuntu here: skipped");
		return;
	}

	const Graphite::ServerLaunch launch =
		Graphite::WslServerLaunch(Clangd(), "graphite-no-such-language-server", Graphite::ServerOverrides{}, Core::WslSharePath{"Ubuntu", "/tmp"}, Core::WslExecutable());
	Graphite::LspClient client{launch, std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\tmp"}};

	Graphite::ServerStatus status;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};

	while (std::chrono::steady_clock::now() < deadline)
	{
		Graphite::LspClient::Drained drained;
		client.Drain(drained);
		status = drained.Status;

		if (status.State != Graphite::ServerState::Starting)
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{50});
	}

	CHECK(status.State == Graphite::ServerState::NotFound);
	CHECK(status.Detail == "graphite-no-such-language-server is not installed in Ubuntu, or not on its PATH");
}

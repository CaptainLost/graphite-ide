#pragma once

#include "Language/Server/LspClient.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerLaunch.h"

#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

enum class FakeServerTemper
{
	LeavesOnExit,
	IgnoresExit
};

struct FakeLanguageServer
{
	std::filesystem::path Script;

	// Written by the server the moment exit reaches it, before it decides whether to leave.
	std::filesystem::path ExitHeard;
};

namespace FakeLanguageServerDetail {

	// Enough of a language server to finish the handshake: it answers initialize under the id a client's
	// first request always carries, and ignores everything else until exit.
	constexpr std::string_view kScriptBody = R"(
$fromClient = [Console]::OpenStandardInput()
$toClient = [Console]::OpenStandardOutput()

function Read-HeaderLine {
	$bytes = New-Object System.Collections.Generic.List[byte]
	while ($true) {
		$next = $fromClient.ReadByte()
		if ($next -lt 0) { return $null }
		if ($next -eq 10) { return [Text.Encoding]::ASCII.GetString($bytes.ToArray()) }
		if ($next -ne 13) { $bytes.Add([byte]$next) }
	}
}

function Read-Message {
	$length = 0
	while ($true) {
		$line = Read-HeaderLine
		if ($null -eq $line) { return $null }
		if ($line -eq '') { break }
		if ($line -match '^Content-Length: *(\d+)') { $length = [int]$Matches[1] }
	}
	$body = New-Object byte[] $length
	$filled = 0
	while ($filled -lt $length) {
		$count = $fromClient.Read($body, $filled, $length - $filled)
		if ($count -le 0) { return $null }
		$filled += $count
	}
	return [Text.Encoding]::UTF8.GetString($body)
}

function Send-Message([string] $json) {
	$body = [Text.Encoding]::UTF8.GetBytes($json)
	$header = [Text.Encoding]::ASCII.GetBytes("Content-Length: $($body.Length)`r`n`r`n")
	$toClient.Write($header, 0, $header.Length)
	$toClient.Write($body, 0, $body.Length)
	$toClient.Flush()
}

while ($null -ne ($message = Read-Message)) {
	if ($message -match '"method":"initialize"') { Send-Message '{"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}}' }
	if ($message -match '"method":"exit"') {
		Set-Content -LiteralPath (Join-Path $PSScriptRoot 'exit-heard') -Value 'heard'
		if ($leavesOnExit) { exit 0 }
	}
}
)";

	constexpr std::chrono::seconds kLongestStart{60};
	constexpr std::chrono::milliseconds kPollInterval{20};
}

// Each server gets a directory of its own under `temp`, so two of them never share an ExitHeard.
inline FakeLanguageServer WriteFakeLanguageServer(TempDirectory& temp, const std::string& name, FakeServerTemper temper)
{
	const std::string leavesOnExit = temper == FakeServerTemper::LeavesOnExit ? "$true" : "$false";
	const std::filesystem::path script =
		temp.MakeFile(name + "/server.ps1", "$leavesOnExit = " + leavesOnExit + "\n" + std::string{FakeLanguageServerDetail::kScriptBody});

	return FakeLanguageServer{.Script = script, .ExitHeard = script.parent_path() / "exit-heard"};
}

// The catalog's clangd row with powershell.exe running the script; the clangd arguments that follow reach the
// script, which ignores them.
inline Graphite::ServerLaunch FakeServerLaunch(const FakeLanguageServer& server)
{
	const Graphite::ServerOverrides noOverrides;
	const Graphite::LaunchPlaces places{.LeadingArguments = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
															 Core::ToUtf8(server.Script)}};

	return Graphite::LaunchFor(*Graphite::FindServer("clangd"), "powershell.exe", noOverrides, places);
}

// PowerShell takes a while to start, and a client is only told a server is ready once the handshake is answered.
inline bool WaitUntilAnswered(Graphite::LspClient& client)
{
	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + FakeLanguageServerDetail::kLongestStart;

	while (std::chrono::steady_clock::now() < deadline)
	{
		Graphite::LspClient::Drained drained;
		client.Drain(drained);

		if (drained.Status.State == Graphite::ServerState::Failed)
		{
			return false;
		}

		if (drained.Status.State != Graphite::ServerState::Starting)
		{
			return true;
		}

		std::this_thread::sleep_for(FakeLanguageServerDetail::kPollInterval);
	}

	return false;
}

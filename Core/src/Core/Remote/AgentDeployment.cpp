#include "corepch.h"

#include "Core/Remote/AgentDeployment.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/ChildProcess.h"
#include "Core/Platform/Hash.h"
#include "Core/Platform/Process.h"
#include "Core/Remote/AgentConnection.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

namespace Core {

	namespace {

		constexpr std::size_t kVersionDigits = 16;
		constexpr std::chrono::seconds kWslCommandTimeout{60};

		std::filesystem::path SystemDirectory()
		{
			std::array<wchar_t, MAX_PATH> buffer{};
			const UINT length = ::GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
			return std::filesystem::path{std::wstring{buffer.data(), length}};
		}

		std::filesystem::path WslExecutable()
		{
			return SystemDirectory() / "wsl.exe";
		}

		std::vector<std::string> InDistro(const std::string& distro, std::vector<std::string> command)
		{
			std::vector<std::string> arguments;

			if (!distro.empty())
			{
				arguments = {"-d", distro};
			}

			arguments.push_back("--exec");
			arguments.insert(arguments.end(), command.begin(), command.end());
			return arguments;
		}

		// Runs wsl.exe and returns what it wrote to stdout, or its failure with that text.
		Result<std::string> RunWsl(const std::vector<std::string>& arguments)
		{
			std::error_code error;

			if (!std::filesystem::exists(WslExecutable(), error))
			{
				return Error{ErrorCode::NotFound, "WSL is not installed"};
			}

			Result<Scope<ChildProcess>> started = ChildProcess::Start(WslExecutable(), arguments, SystemDirectory());

			if (!started)
			{
				return started.GetError();
			}

			ChildProcess& process = *started.GetValue();
			std::string output;
			std::array<char, 4096> buffer{};

			for (std::size_t count = process.Read(buffer); count > 0; count = process.Read(buffer))
			{
				output.append(buffer.data(), count);
			}

			process.WaitForExit(kWslCommandTimeout);

			const std::optional<std::uint32_t> exitCode = process.GetExitCode();

			if (!exitCode || *exitCode != 0)
			{
				// wsl.exe's own messages are UTF-16; a byte-wise read leaves NULs between the letters.
				std::erase(output, '\0');
				return Error{ErrorCode::IoFailure, std::format("wsl.exe failed: {}", output)};
			}

			return output;
		}

		std::string ToHex(const std::vector<std::uint8_t>& bytes)
		{
			std::string hex;

			for (const std::uint8_t byte : bytes)
			{
				hex += std::format("{:02x}", byte);
			}

			return hex;
		}
	}

	std::string AgentVersionOf(std::string_view binaryBytes)
	{
		const Result<std::vector<std::uint8_t>> digest = HashBytes(HashAlgorithm::Sha256, binaryBytes);

		if (!digest)
		{
			return {};
		}

		return ToHex(digest.GetValue()).substr(0, kVersionDigits);
	}

	std::filesystem::path SharePathOf(const std::string& distro, std::string_view linuxPath)
	{
		std::string share = "\\\\wsl.localhost\\" + distro;

		for (const char character : linuxPath)
		{
			share.push_back(character == '/' ? '\\' : character);
		}

		return FromUtf8(share);
	}

	AgentInstall AgentInstallFor(const std::string& distro, const std::string& home, const std::string& version)
	{
		const std::string linuxPath = home + "/.graphite/agent/" + version + "/graphite-agent";

		return AgentInstall{
			.Distro = distro,
			.Home = home,
			.Version = version,
			.LinuxPath = linuxPath,
			.SharePath = SharePathOf(distro, linuxPath),
		};
	}

	bool IsWslAvailable()
	{
		return RunWsl({"--exec", "true"}).HasValue();
	}

	Result<AgentInstall> InstallAgent(const std::string& distro, const std::filesystem::path& agentBinary)
	{
		const Result<std::string> binary = ReadFileBytes(agentBinary);

		if (!binary)
		{
			return Error{binary.GetError().Code, std::format("cannot read the agent '{}': {}", ToUtf8(agentBinary), binary.GetError().Message)};
		}

		// One call for both: the distro's own name, which the share needs, and its home.
		const Result<std::string> answer = RunWsl(InDistro(distro, {"sh", "-c", R"(printf '%s\n%s' "$WSL_DISTRO_NAME" "$HOME")"}));

		if (!answer)
		{
			return answer.GetError();
		}

		const std::string& text = answer.GetValue();
		const std::size_t newline = text.find('\n');

		if (newline == std::string::npos || newline == 0 || newline + 1 >= text.size())
		{
			return Error{ErrorCode::IoFailure, std::format("the distro did not say its name and home: '{}'", text)};
		}

		const AgentInstall install = AgentInstallFor(text.substr(0, newline), text.substr(newline + 1), AgentVersionOf(binary.GetValue()));

		// The folder is named by the binary's hash, and the file only ever arrives whole (moved
		// into place below), so one of the right size is this build.
		std::error_code error;
		const bool alreadyThere = std::filesystem::exists(install.SharePath, error) &&
								  std::filesystem::file_size(install.SharePath, error) == binary.GetValue().size();

		if (alreadyThere)
		{
			return install;
		}

		std::filesystem::create_directories(install.SharePath.parent_path(), error);

		if (error)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot create '{}': {}", ToUtf8(install.SharePath.parent_path()), error.message())};
		}

		// Written whole under another name first, then moved into place inside the distro, which
		// is also where it can be made executable.
		const std::string partialName = install.LinuxPath + ".part";
		{
			std::ofstream partial{SharePathOf(install.Distro, partialName), std::ios::binary | std::ios::trunc};
			partial.write(binary.GetValue().data(), static_cast<std::streamsize>(binary.GetValue().size()));

			if (!partial.good())
			{
				return Error{ErrorCode::IoFailure, std::format("cannot copy the agent into '{}'", install.Distro)};
			}
		}

		const Result<std::string> placed = RunWsl(InDistro(
			install.Distro, {"sh", "-c", R"(chmod 755 "$1" && mv -f "$1" "$2")", "sh", partialName, install.LinuxPath}));

		if (!placed)
		{
			return placed.GetError();
		}

		return install;
	}

	Result<Scope<AgentPipe>> StartAgent(const AgentInstall& install)
	{
		Result<Scope<ChildProcess>> started =
			ChildProcess::Start(WslExecutable(), InDistro(install.Distro, {install.LinuxPath}), SystemDirectory(), {},
								ChildProcessOptions{.PipeErrors = false, .EndJobOnClose = true});

		if (!started)
		{
			return started.GetError();
		}

		return PipeOver(std::move(started.GetValue()));
	}

	std::filesystem::path AgentBinaryBesideExecutable()
	{
		const Result<std::filesystem::path> directory = ExecutableDirectory();
		return directory ? directory.GetValue() / "graphite-agent" : std::filesystem::path{"graphite-agent"};
	}
}

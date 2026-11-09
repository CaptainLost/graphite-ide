// graphite-agent: Graphite's helper inside a WSL distro. Graphite starts it through wsl.exe and
// talks to it in frames over its stdin and stdout (ADR 0695); it ends when stdin closes or
// Graphite says bye. Its own messages go to stderr.

#include "corepch.h"

#include "Session/AgentInstall.h"
#include "Session/AgentLogging.h"
#include "Session/AgentSession.h"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

	// Short enough that a settled batch goes out within a frame or so of its quiet period ending.
	constexpr int kTickMilliseconds = 50;

	constexpr std::size_t kReadBufferBytes = 64 * 1024;

	bool WriteAll(std::string_view bytes)
	{
		while (!bytes.empty())
		{
			const ssize_t written = ::write(STDOUT_FILENO, bytes.data(), bytes.size());

			if (written < 0)
			{
				if (errno == EINTR)
				{
					continue;
				}

				return false;
			}

			bytes.remove_prefix(static_cast<std::size_t>(written));
		}

		return true;
	}

	std::filesystem::path Self()
	{
		std::error_code error;
		return std::filesystem::read_symlink("/proc/self/exe", error);
	}
}

int main()
{
	// A Graphite that went away mid-write must end the agent through a failed write, not a signal.
	std::signal(SIGPIPE, SIG_IGN);

	Agent::SendLogsToStandardError();

	const std::filesystem::path self = Self();
	Agent::RemoveOtherVersions(self);

	Agent::AgentSession session{Agent::VersionOf(self)};
	std::array<char, kReadBufferBytes> buffer{};

	while (!session.HasEnded())
	{
		std::vector<pollfd> watched{pollfd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0}};

		for (const int descriptor : session.WatchDescriptors())
		{
			watched.push_back(pollfd{.fd = descriptor, .events = POLLIN, .revents = 0});
		}

		const int ready = ::poll(watched.data(), watched.size(), kTickMilliseconds);

		if (ready < 0 && errno != EINTR)
		{
			return 1;
		}

		if ((watched.front().revents & (POLLIN | POLLHUP | POLLERR)) != 0)
		{
			const ssize_t bytesRead = ::read(STDIN_FILENO, buffer.data(), buffer.size());

			if (bytesRead <= 0)
			{
				break; // Graphite closed the pipe
			}

			session.Receive(std::string_view{buffer.data(), static_cast<std::size_t>(bytesRead)});
		}

		session.Tick();

		if (!WriteAll(session.TakeOutput()))
		{
			break;
		}
	}

	return 0;
}

#include "Wsl/WslIgnores.h"

#include "Wsl/WslAgents.h"

#include "Core/Base/Log.h"

#include <chrono>
#include <utility>

namespace Graphite {

	namespace {

		// The Explorer asks on the UI thread: never wait for an agent being started.
		constexpr std::chrono::milliseconds kNoStartWait{0};
	}

	Core::Result<Core::Scope<RepositoryIgnores>> WslIgnores::Open(WslAgents& agents, const Core::WslSharePath& root, Lister list)
	{
		// A distro whose agent is still starting is asked again shortly (the Workspace retries on
		// Cancelled); only that answer may say Cancelled, so a request that timed out is not retried.
		const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = agents.Get(root.Distro, kNoStartWait);

		if (!connection)
		{
			return connection.GetError();
		}

		auto git = std::make_unique<WslGit>(agents, root, WslGit::Reach::SearchUpward);
		Core::Result<WslGit::IgnoreSetup> setup = git->QueryIgnoreSetup(kNoStartWait);

		if (!setup)
		{
			const Core::Error& failure = setup.GetError();
			return failure.Code == Core::ErrorCode::Cancelled ? Core::Error{Core::ErrorCode::IoFailure, failure.Message} : failure;
		}

		return Core::Scope<RepositoryIgnores>{new WslIgnores(std::move(git), std::move(setup.GetValue()), std::move(list))};
	}

	WslIgnores::WslIgnores(std::unique_ptr<WslGit> git, WslGit::IgnoreSetup setup, Lister list)
		: m_Git(std::move(git)), m_Setup(std::move(setup)), m_List(std::move(list))
	{
	}

	bool WslIgnores::IsIgnored(const std::filesystem::path& path, [[maybe_unused]] bool isDirectory)
	{
		const std::filesystem::path folder = path.parent_path();
		const std::string name = Core::ToUtf8(path.filename());
		const auto kept = m_Answers.find(folder);

		if (kept != m_Answers.end())
		{
			const auto answer = kept->second.IgnoredByName.find(name);

			if (answer != kept->second.IgnoredByName.end())
			{
				return answer->second;
			}

			// A folder that could not be answered stays "not ignored" until the tree asks again.
			if (kept->second.Failed)
			{
				return false;
			}
		}

		// Not asked about yet, or made since the folder was asked about.
		const FolderAnswer& asked = Ask(folder);
		const auto answer = asked.IgnoredByName.find(name);
		return answer != asked.IgnoredByName.end() && answer->second;
	}

	const WslIgnores::FolderAnswer& WslIgnores::Ask(const std::filesystem::path& folder)
	{
		FolderAnswer& answer = m_Answers[folder];
		answer = FolderAnswer{};
		const Core::Result<std::vector<Core::DirectoryEntry>> entries = m_List(folder);
		const Core::Result<std::vector<bool>> outcome =
			entries ? m_Git->QueryIgnored(folder, entries.GetValue(), kNoStartWait) : Core::Result<std::vector<bool>>{entries.GetError()};

		if (!outcome)
		{
			answer.Failed = true;

			if (m_Reported.insert(folder).second)
			{
				APP_WARN("Cannot ask the WSL agent what git ignores in '{}': {}", Core::ToUtf8(folder), outcome.GetError().Message);
			}

			return answer;
		}

		for (std::size_t index = 0; index < entries.GetValue().size(); ++index)
		{
			answer.IgnoredByName[entries.GetValue()[index].Name] = outcome.GetValue()[index];
		}

		return answer;
	}

	std::vector<std::filesystem::path> WslIgnores::RuleFilesOutside([[maybe_unused]] const std::filesystem::path& folder)
	{
		// Asked of the agent for the open folder when this was opened, which is the only folder the
		// Workspace asks about.
		return m_Setup.RuleFiles;
	}
}

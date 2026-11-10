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
		// Cancelled); only that answer may say Cancelled, so a request that timed out is not retried,
		// and a distro whose agent could not start answers its failure rather than being started anew
		// on every retry.
		const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = agents.Peek(root.Distro);

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

	bool WslIgnores::IsIgnored(const std::filesystem::path& path, bool isDirectory)
	{
		const std::filesystem::path folder = path.parent_path();
		const std::string name = Core::ToUtf8(path.filename());
		const auto kept = m_Answers.find(folder);

		if (kept != m_Answers.end())
		{
			const auto answer = kept->second.ByName.find(name);

			if (answer != kept->second.ByName.end() && answer->second.IsDirectory == isDirectory)
			{
				return answer->second.Ignored;
			}

			// A folder that could not be answered stays "not ignored" until the tree asks again.
			if (kept->second.Failed)
			{
				return false;
			}
		}

		if (m_GaveUp)
		{
			return false;
		}

		// Not asked about yet, made since the folder was asked about, or now the other kind.
		const FolderAnswer& asked = Ask(folder);
		const auto answer = asked.ByName.find(name);
		return answer != asked.ByName.end() && answer->second.Ignored;
	}

	void WslIgnores::ForgetAnswers()
	{
		m_Answers.clear();
		m_GaveUp = false;
	}

	void WslIgnores::PrepareFolder(const std::filesystem::path& folder, const std::vector<Core::DirectoryEntry>& entries)
	{
		if (!m_GaveUp)
		{
			static_cast<void>(Ask(folder, entries));
		}
	}

	const WslIgnores::FolderAnswer& WslIgnores::Ask(const std::filesystem::path& folder)
	{
		const Core::Result<std::vector<Core::DirectoryEntry>> entries = m_List(folder);

		if (!entries)
		{
			FolderAnswer& answer = m_Answers[folder];
			answer = FolderAnswer{.Failed = true};

			if (m_Reported.insert(folder).second)
			{
				APP_WARN("Cannot list '{}' to ask what git ignores in it: {}", Core::ToUtf8(folder), entries.GetError().Message);
			}

			return answer;
		}

		return Ask(folder, entries.GetValue());
	}

	const WslIgnores::FolderAnswer& WslIgnores::Ask(const std::filesystem::path& folder, const std::vector<Core::DirectoryEntry>& entries)
	{
		FolderAnswer& answer = m_Answers[folder];
		answer = FolderAnswer{};
		const Core::Result<std::vector<bool>> ignored = m_Git->QueryIgnored(folder, entries, kNoStartWait);

		if (!ignored)
		{
			answer.Failed = true;

			// Every other folder would wait as long on the UI thread: ask none until the tree asks again.
			if (ignored.GetError().Code == Core::ErrorCode::Cancelled)
			{
				m_GaveUp = true;
			}

			if (m_Reported.insert(folder).second)
			{
				APP_WARN("Cannot ask the WSL agent what git ignores in '{}': {}", Core::ToUtf8(folder), ignored.GetError().Message);
			}

			return answer;
		}

		for (std::size_t index = 0; index < entries.size(); ++index)
		{
			answer.ByName[entries[index].Name] = EntryAnswer{.Ignored = ignored.GetValue()[index], .IsDirectory = entries[index].IsDirectory};
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

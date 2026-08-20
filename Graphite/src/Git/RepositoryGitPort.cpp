#include "Git/RepositoryGitPort.h"

#include "Core/Fs/FileSystem.h"

#include <unordered_map>
#include <utility>

namespace Graphite {

	RepositoryGitPort::RepositoryGitPort(std::filesystem::path root)
		: m_Root(Core::ToUtf8(root))
	{
	}

	void RepositoryGitPort::SetRoot(std::filesystem::path root)
	{
		const std::lock_guard<std::mutex> lock{m_RootMutex};
		m_Root = Core::ToUtf8(root);
	}

	Core::Repository* RepositoryGitPort::RepositoryForThisThread()
	{
		std::string root;

		{
			const std::lock_guard<std::mutex> lock{m_RootMutex};
			root = m_Root;
		}

		if (root.empty())
		{
			return nullptr;
		}

		static thread_local std::unordered_map<std::string, Core::Repository> handles;

		if (const auto existing = handles.find(root); existing != handles.end())
		{
			return &existing->second;
		}

		auto opened = Core::Repository::Open(Core::FromUtf8(root));

		if (!opened.HasValue())
		{
			return nullptr;
		}

		const auto inserted = handles.emplace(root, std::move(opened).GetValue());
		return &inserted.first->second;
	}

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryStatus()
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryStatus();
	}

	Core::Result<std::vector<Core::Branch>> RepositoryGitPort::QueryBranches()
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryBranches();
	}

	Core::Result<std::vector<Core::CommitSummary>> RepositoryGitPort::QueryLog(
		const Core::LogRequest& request)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryLog(request);
	}
}

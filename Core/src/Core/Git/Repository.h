#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Requests.h"

#include <filesystem>
#include <vector>

struct git_repository;

namespace Core {

	class Repository
	{
	public:
		static Result<Repository> Open(const std::filesystem::path& anyPathInside);

		~Repository();

		Repository(Repository&& other) noexcept;
		Repository& operator=(Repository&& other) noexcept;

		Repository(const Repository&) = delete;
		Repository& operator=(const Repository&) = delete;

		bool IsIgnored(const std::filesystem::path& path) const;

		// Newest first, in topological then date order — the order a history is read in.
		// A repository whose HEAD is unborn answers with an empty list, not an error.
		Result<std::vector<CommitSummary>> QueryLog(const LogRequest& request) const;

		const std::filesystem::path& GetWorkingDirectory() const
		{
			return m_WorkingDirectory;
		}

	private:
		Repository() = default;

		git_repository* m_Handle = nullptr;
		std::filesystem::path m_WorkingDirectory;
	};
}
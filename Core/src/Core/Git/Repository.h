#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

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
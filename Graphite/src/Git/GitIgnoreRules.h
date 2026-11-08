#pragma once

#include "Core/Git/Repository.h"

#include <filesystem>
#include <optional>

namespace Graphite {

	// Whether git ignores a path under a folder, as the Explorer, search, the language servers and the analysis all ask
	// it, so they agree.
	// WARNING: Made on the thread that asks, never shared with the workspace's repository: libgit2 handles are not safe
	// to use from two threads. Outside any repository nothing is ignored.
	class GitIgnoreRules
	{
	public:
		explicit GitIgnoreRules(const std::filesystem::path& root);

		bool IsIgnored(const std::filesystem::path& path) const;

	private:
		std::optional<Core::Repository> m_Repository;
	};
}

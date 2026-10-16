#pragma once

#include "Core/Git/Repository.h"
#include "Core/Jobs/CancellationToken.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace Graphite {

	struct Solution;

	using IgnorePredicate = std::function<bool(const std::filesystem::path& path,
											   bool isDirectory)>;

	// WARNING: Made on the thread that walks, never shared with the workspace's repository: libgit2 handles are not safe
	// to use from two threads. Outside any repository nothing is ignored.
	class GitIgnoreRules
	{
	public:
		explicit GitIgnoreRules(const std::filesystem::path& root);

		bool IsIgnored(const std::filesystem::path& path) const;

	private:
		std::optional<Core::Repository> m_Repository;
	};

	struct EnumerationInput
	{
		std::filesystem::path Root;
		IgnorePredicate Ignored;
	};

	void EnumerateFiles(const EnumerationInput& input, const Core::CancellationToken& token,
						const std::function<void(const std::filesystem::path&)>& onFile);

	void EnumerateProjectFiles(const Solution& solution,
							   const std::function<void(const std::filesystem::path&)>& onFile);
}

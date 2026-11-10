#pragma once

#include "Core/Base/Pointers.h"
#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <vector>

namespace Graphite {

	// What the Explorer asks of the repository around the open folder while it lists it: which
	// entries git ignores, and which rule files outside the folder a watch on it cannot see change.
	// Asked on the UI thread.
	class RepositoryIgnores
	{
	public:
		virtual ~RepositoryIgnores() = default;

		// Empty for a bare repository.
		virtual std::filesystem::path GetWorkingDirectory() const = 0;

		virtual bool IsIgnored(const std::filesystem::path& path, bool isDirectory) = 0;

		virtual std::vector<std::filesystem::path> RuleFilesOutside(const std::filesystem::path& folder) = 0;

		// Called before the tree asks about every entry again: an answer kept from before is stale.
		virtual void ForgetAnswers() {}
	};

	// NotFound when the folder is in no repository; Cancelled when the answer is not ready yet and
	// opening again shortly may succeed (a WSL distro's agent still starting).
	using IgnoresOpener = std::function<Core::Result<Core::Scope<RepositoryIgnores>>(const std::filesystem::path& root)>;

	// The repository around root, opened by libgit2 on this machine.
	Core::Result<Core::Scope<RepositoryIgnores>> OpenLocalRepositoryIgnores(const std::filesystem::path& root);
}

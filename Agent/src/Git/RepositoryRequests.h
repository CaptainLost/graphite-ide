#pragma once

#include "Git/GitWorker.h"

#include "Core/Base/Result.h"
#include "Core/Git/Repository.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// What the agent's git handlers share: the repository each request names, opened once and kept,
// and the shapes their answers take.
namespace Agent {

	// One handle per folder asked about, kept between requests: libgit2 reads the index and config
	// once, as the host's port does with its own cache. Used on one worker only.
	class RepositoryCache
	{
	public:
		// writes: InMemory for handles whose checks create objects that must never reach the repository.
		explicit RepositoryCache(Core::ObjectWrites writes = Core::ObjectWrites::OnDisk) : m_Writes(writes) {}

		Core::Result<Core::Repository*> Get(const std::string& root, bool exactly);
		void Forget(const std::string& root);

	private:
		// The shallow file as the handle was opened: present, its size and write time.
		struct ShallowStamp
		{
			bool Exists = false;
			std::uintmax_t Size = 0;
			std::filesystem::file_time_type Written{};

			bool operator==(const ShallowStamp&) const = default;
		};

		struct Cached
		{
			Core::Repository Repository;
			ShallowStamp Shallow;
		};

		static ShallowStamp StampOf(const Core::Repository& repository);

		Core::ObjectWrites m_Writes;
		std::map<std::string, Cached> m_Repositories;
	};

	struct Asked
	{
		std::string Root;
		bool Exactly = false;
	};

	// The request's absolute "root" and its "reach" ("exactly" or "up").
	Core::Result<Asked> AskedOf(const nlohmann::json& arguments);

	// The request's "paths", relative to the working directory.
	std::vector<std::filesystem::path> PathsOf(const nlohmann::json& arguments);

	// A result too large or too nested for the header goes in the payload as JSON.
	GitAnswer AsPayload(const nlohmann::json& result);

	Core::Result<GitAnswer> Done(const Core::Result<void>& outcome);

	using RepositoryHandler = std::function<Core::Result<GitAnswer>(Core::Repository& repository, const nlohmann::json& arguments)>;

	// Opens the request's repository through the cache, then answers with handler.
	GitHandler OnRepository(std::shared_ptr<RepositoryCache> cache, RepositoryHandler handler);

	// The same, for a handler that reads the request's payload too.
	using RepositoryPayloadHandler =
		std::function<Core::Result<GitAnswer>(Core::Repository& repository, const nlohmann::json& arguments, const std::string& payload)>;
	GitHandler OnRepository(std::shared_ptr<RepositoryCache> cache, RepositoryPayloadHandler handler);

	// The working directory in the spelling of the root Graphite asked with: libgit2 resolves
	// symbolic links (~/code -> /data/code), and the host names files by the folder it opened, so
	// a resolved spelling would open the same file under another path. Empty for a bare
	// repository, which has no working directory.
	std::string SpelledLikeRoot(const std::filesystem::path& workingDirectory, const std::string& root);

	// One read's bytes, or absent; the sides one after another in the payload, each listed in the
	// result's "sides" with its size or as absent.
	void AddSide(GitAnswer& answer, const std::optional<std::string>& side);

	// A file's path relative to the working directory when git tracks files there: empty when the
	// file is outside the working directory, ignored, inside a nested checkout or a submodule.
	// The asked path is resolved first, as libgit2 resolves symbolic links in the working directory.
	std::optional<std::filesystem::path> TrackableRelativePath(const Core::Repository& repository, const std::filesystem::path& asked);

	// Whether a folder between the working directory and the file holds a checkout of its own.
	bool IsInsideNestedCheckout(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative);

	// A read that git answers NotFound for is an absent side; any other failure stays one.
	Core::Result<std::optional<std::string>> PresentOrAbsent(Core::Result<std::string> read);
}

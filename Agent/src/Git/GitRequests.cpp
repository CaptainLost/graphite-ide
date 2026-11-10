#include "corepch.h"

#include "Git/GitRequests.h"

#include "Git/HistoryRequests.h"
#include "Git/RepositoryRequests.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Remote/GitCodec.h"

#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace Agent {

	namespace {

		using nlohmann::json;

		// The same rule as the host's port (MarkNestedCheckouts): asked once per untracked folder
		// here, on the worker, so the host's decorations answer from the status alone.
		void MarkNestedCheckouts(const std::filesystem::path& workingDirectory, Core::WorkingTreeStatus& status)
		{
			for (Core::FileStatus& entry : status.Entries)
			{
				if (entry.IsUntracked && entry.IsDirectory)
				{
					std::error_code error;
					entry.HoldsCheckout = std::filesystem::exists(workingDirectory / entry.Path / ".git", error);
				}
			}
		}

		bool IsInsideNestedCheckout(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative)
		{
			std::filesystem::path directory = workingDirectory;

			for (const std::filesystem::path& part : relative.parent_path())
			{
				if (part.empty() || part == ".")
				{
					continue;
				}

				directory /= part;
				std::error_code error;

				if (std::filesystem::exists(directory / ".git", error))
				{
					return true;
				}
			}

			return false;
		}

		enum class Side
		{
			Head,
			Index,
			WorkTree
		};

		// One side's bytes: absent when git has no such file there (NotFound), the error otherwise.
		Core::Result<std::optional<std::string>> ReadSide(Core::Repository& repository, Side side, const std::filesystem::path& path)
		{
			Core::Result<std::string> read = Core::Error{Core::ErrorCode::NotFound, ""};

			switch (side)
			{
			case Side::Head:
			{
				const Core::Result<Core::Oid> head = repository.HeadCommit();

				if (!head)
				{
					return head.GetError().Code == Core::ErrorCode::NotFound ? Core::Result<std::optional<std::string>>{std::nullopt}
																			 : Core::Result<std::optional<std::string>>{head.GetError()};
				}

				read = repository.ReadBlob(path, head.GetValue());
				break;
			}
			case Side::Index:
				read = repository.ReadIndexBlob(path);
				break;
			case Side::WorkTree:
				read = repository.ReadWorkingTreeFile(path);
				break;
			}

			return PresentOrAbsent(std::move(read));
		}

		// Sides one after another in the payload, each listed with its size or as absent.
		Core::Result<GitAnswer> ReadSides(Core::Repository& repository, const std::vector<std::pair<Side, std::filesystem::path>>& reads)
		{
			GitAnswer answer;
			answer.Result["sides"] = json::array();

			for (const auto& [side, path] : reads)
			{
				const Core::Result<std::optional<std::string>> read = ReadSide(repository, side, path);

				if (!read)
				{
					return read.GetError();
				}

				AddSide(answer, read.GetValue());
			}

			return answer;
		}

		std::optional<Side> SideNamed(const std::string& name)
		{
			if (name == "head")
			{
				return Side::Head;
			}

			if (name == "index")
			{
				return Side::Index;
			}

			if (name == "workTree")
			{
				return Side::WorkTree;
			}

			return std::nullopt;
		}
	}

	GitHandlers DefaultGitHandlers()
	{
		auto cache = std::make_shared<RepositoryCache>();
		GitHandlers handlers;

		handlers["git.status"] = OnRepository(cache, [](Core::Repository& repository, const json&) -> Core::Result<GitAnswer> {
			Core::Result<Core::WorkingTreeStatus> status = repository.QueryStatus();

			if (!status)
			{
				return status.GetError();
			}

			MarkNestedCheckouts(repository.GetWorkingDirectory(), status.GetValue());
			return AsPayload(Core::ToJson(status.GetValue()));
		});

		handlers["git.untrackedIn"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::WorkingTreeStatus> status = repository.QueryUntrackedIn(arguments.value("directory", std::string{}));
			return status ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(status.GetValue()))} : Core::Result<GitAnswer>{status.GetError()};
		});

		handlers["git.workingDirectory"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			return GitAnswer{.Result = {{"path", SpelledLikeRoot(repository.GetWorkingDirectory(), arguments.value("root", std::string{}))}}};
		});

		handlers["git.head"] = OnRepository(cache, [](Core::Repository& repository, const json&) -> Core::Result<GitAnswer> {
			const Core::Result<Core::HeadState> head = repository.QueryHead();
			return head ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(head.GetValue()))} : Core::Result<GitAnswer>{head.GetError()};
		});

		handlers["git.diff"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::DiffRequest> request = Core::DiffRequestFromJson(arguments.value("request", json::object()));

			if (!request)
			{
				return request.GetError();
			}

			const Core::Result<Core::Diff> diff = repository.QueryDiff(request.GetValue());
			return diff ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(diff.GetValue()))} : Core::Result<GitAnswer>{diff.GetError()};
		});

		handlers["git.readSides"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			std::vector<std::pair<Side, std::filesystem::path>> reads;

			for (const json& read : arguments.value("reads", json::array()))
			{
				const std::optional<Side> side = read.is_object() ? SideNamed(read.value("side", std::string{})) : std::nullopt;

				if (!side)
				{
					return Core::Error{Core::ErrorCode::InvalidArgument, "a read names no side"};
				}

				reads.emplace_back(*side, read.value("path", std::string{}));
			}

			return ReadSides(repository, reads);
		});

		// Not through OnRepository: a folder with no repository, or one that cannot be opened, has
		// nothing trackable rather than an error, as the host's port answers for a local file.
		handlers["git.bases"] = [cache](const Core::AgentRequest& request) -> Core::Result<GitAnswer> {
			const Core::Result<Asked> asked = AskedOf(request.Arguments);

			if (!asked)
			{
				return asked.GetError();
			}

			const Core::Result<Core::Repository*> opened = cache->Get(asked.GetValue().Root, asked.GetValue().Exactly);

			if (!opened)
			{
				return GitAnswer{.Result = {{"trackable", false}}};
			}

			Core::Repository& repository = *opened.GetValue();
			const json& arguments = request.Arguments;

			// libgit2 resolves symbolic links in the working directory, so the file is resolved too
			// before the two are compared.
			std::error_code resolveError;
			const std::filesystem::path askedPath{arguments.value("path", std::string{})};
			const std::filesystem::path absolute = std::filesystem::weakly_canonical(askedPath, resolveError);
			const std::filesystem::path workingDirectory = Core::NormalizeDirectory(repository.GetWorkingDirectory());
			const std::filesystem::path relative = (resolveError ? askedPath : absolute).lexically_relative(workingDirectory);

			// Not trackable: outside the working directory, ignored, or another repository's.
			const bool outside = relative.empty() || *relative.begin() == "..";

			if (outside || repository.IsIgnored(absolute) || IsInsideNestedCheckout(workingDirectory, relative) ||
				repository.IsInsideSubmodule(relative))
			{
				return GitAnswer{.Result = {{"trackable", false}}};
			}

			Core::Result<GitAnswer> sides = ReadSides(repository, {{Side::Head, relative}, {Side::Index, relative}});

			if (!sides)
			{
				return sides.GetError();
			}

			sides.GetValue().Result["trackable"] = true;
			return sides;
		};

		handlers["git.stage"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) {
			return Done(repository.Stage(PathsOf(arguments)));
		});

		handlers["git.unstage"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) {
			return Done(repository.Unstage(PathsOf(arguments)));
		});

		handlers["git.discard"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) {
			return Done(repository.DiscardWorkTree(PathsOf(arguments)));
		});

		handlers["git.deleteUntracked"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) {
			return Done(repository.DeleteUntracked(PathsOf(arguments)));
		});

		handlers["git.discardAll"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) {
			const bool withUntracked = arguments.value("untracked", false);
			return Done(repository.DiscardAll(withUntracked ? Core::UntrackedFiles::Included : Core::UntrackedFiles::Excluded));
		});

		handlers["git.commit"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::CommitRequest> request = Core::CommitRequestFromJson(arguments.value("request", json::object()));

			if (!request)
			{
				return request.GetError();
			}

			const Core::Result<Core::Oid> committed = repository.Commit(request.GetValue());
			return committed ? Core::Result<GitAnswer>{GitAnswer{.Result = {{"oid", committed.GetValue().ToHex()}}}}
							 : Core::Result<GitAnswer>{committed.GetError()};
		});

		// Not through the cache: an init is safe only where Open finds nothing (the host's port
		// asks the same way), and the new repository is opened fresh by the next request.
		handlers["git.init"] = [cache](const Core::AgentRequest& request) -> Core::Result<GitAnswer> {
			const Core::Result<Asked> asked = AskedOf(request.Arguments);

			if (!asked)
			{
				return asked.GetError();
			}

			const Core::Result<Core::Repository> existing = Core::Repository::Open(asked.GetValue().Root);

			if (existing)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "the folder is already inside a repository"};
			}

			if (existing.GetError().Code != Core::ErrorCode::NotFound)
			{
				return existing.GetError();
			}

			const Core::Result<Core::Repository> created = Core::Repository::Init(asked.GetValue().Root);

			if (!created)
			{
				return created.GetError();
			}

			cache->Forget(asked.GetValue().Root);
			return GitAnswer{};
		};

		AddHistoryHandlers(handlers, cache);
		return handlers;
	}

	GitHandlers DefaultQuickGitHandlers()
	{
		auto cache = std::make_shared<RepositoryCache>();
		GitHandlers handlers;

		// What the Explorer needs once per folder it opens: where the repository is, and the rule
		// files above the folder that a watch on the folder cannot see change.
		handlers["git.ignoreSetup"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const std::string root = arguments.value("root", std::string{});
			json ruleFiles = json::array();

			for (const std::filesystem::path& file : repository.IgnoreRuleFilesOutside(root))
			{
				ruleFiles.push_back(file.string()); // not a path conversion: Linux paths are their bytes
			}

			return GitAnswer{.Result = {{"workingDirectory", SpelledLikeRoot(repository.GetWorkingDirectory(), root)}, {"ruleFiles", std::move(ruleFiles)}}};
		});

		// Every entry of one folder at once, in the order asked: the Explorer asks as it lists.
		handlers["git.ignored"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			// libgit2 resolves symbolic links in the working directory, so the folder is resolved too.
			const std::filesystem::path asked{arguments.value("folder", std::string{})};
			std::error_code resolveError;
			const std::filesystem::path resolved = std::filesystem::weakly_canonical(asked, resolveError);
			const std::filesystem::path folder = resolveError ? asked : resolved;
			json ignored = json::array();

			for (const json& entry : arguments.value("entries", json::array()))
			{
				const std::string name = entry.is_object() ? entry.value("name", std::string{}) : std::string{};
				ignored.push_back(!name.empty() && repository.IsIgnored(folder / name));
			}

			return GitAnswer{.Result = {{"ignored", std::move(ignored)}}};
		});

		return handlers;
	}
}

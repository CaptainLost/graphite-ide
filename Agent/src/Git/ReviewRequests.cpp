#include "corepch.h"

#include "Git/ReviewRequests.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Remote/GitCodec.h"

#include <sys/stat.h>

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace Agent {

	using nlohmann::json;

	namespace {

		// The most commits a review lists, as the host's port lists locally.
		constexpr std::size_t kMostCommitsListedInRange = 500;

		std::optional<Core::Oid> OidOf(const json& arguments, const char* name)
		{
			const auto found = arguments.find(name);
			return found != arguments.end() && found->is_string() ? Core::Oid::FromHex(found->get<std::string>()) : std::nullopt;
		}

		Core::Result<Core::Oid> RequiredOid(const json& arguments, const char* name)
		{
			const std::optional<Core::Oid> id = OidOf(arguments, name);

			if (!id)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, std::format("the request has no commit id \"{}\"", name)};
			}

			return *id;
		}

		std::int64_t SecondsOf(std::chrono::sys_seconds when)
		{
			return when.time_since_epoch().count();
		}

		// Seconds since the epoch; the standard library's file clock has no conversion in libc++.
		std::optional<std::int64_t> ModifiedAt(const std::filesystem::path& path)
		{
			struct stat status{};

			if (::stat(path.c_str(), &status) != 0)
			{
				return std::nullopt;
			}

			return static_cast<std::int64_t>(status.st_mtim.tv_sec);
		}

		// As the host's port: no repository, a file it does not track, an unborn HEAD or a file
		// HEAD lacks is not trackable; an empty file has an empty blame without asking libgit2.
		Core::Result<GitAnswer> BlameAtHead(Core::Repository& repository, const json& arguments)
		{
			const std::optional<std::filesystem::path> relative = TrackableRelativePath(repository, arguments.value("path", std::string{}));

			if (!relative)
			{
				return GitAnswer{.Result = {{"trackable", false}}};
			}

			const Core::Result<Core::Oid> head = repository.HeadCommit();

			if (!head)
			{
				return head.GetError().Code == Core::ErrorCode::NotFound ? Core::Result<GitAnswer>{GitAnswer{.Result = {{"trackable", false}}}}
																		 : Core::Result<GitAnswer>{head.GetError()};
			}

			Core::Result<std::string> bytes = repository.ReadBlob(*relative, head.GetValue());

			if (!bytes)
			{
				return bytes.GetError().Code == Core::ErrorCode::NotFound ? Core::Result<GitAnswer>{GitAnswer{.Result = {{"trackable", false}}}}
																		  : Core::Result<GitAnswer>{bytes.GetError()};
			}

			Core::Blame blame;

			if (bytes.GetValue().empty())
			{
				blame.At = head.GetValue();
				blame.File = *relative;
			}
			else
			{
				Core::Result<Core::Blame> asked = repository.QueryBlame(Core::BlameRequest{*relative, head.GetValue()});

				if (!asked)
				{
					return asked.GetError();
				}

				blame = std::move(asked.GetValue());
			}

			GitAnswer answer{.Result = {{"trackable", true}, {"headSize", bytes.GetValue().size()}}};
			answer.Payload = std::move(bytes.GetValue());
			answer.Payload += Core::ToJson(blame).dump(-1, ' ', false, json::error_handler_t::replace);
			return answer;
		}

		Core::Result<GitAnswer> ResolveRange(Core::Repository& repository, const json& arguments)
		{
			const std::string branch = arguments.value("branch", std::string{});
			const std::string base = arguments.value("base", std::string{});
			GitAnswer answer{.Result = {{"problem", "none"}, {"complete", true}}};

			const Core::Result<Core::Oid> tip = repository.ResolveReference(branch);

			if (!tip)
			{
				if (tip.GetError().Code != Core::ErrorCode::NotFound)
				{
					return tip.GetError();
				}

				answer.Result["problem"] = "branchMissing";
				return answer;
			}

			// A failed lookup reads as held nowhere, as the host's port reads it.
			if (const Core::Result<std::optional<std::filesystem::path>> holding = repository.WorktreeHolding(branch); holding && holding.GetValue())
			{
				answer.Result["worktree"] = holding.GetValue()->string(); // not a path conversion: Linux paths are their bytes
			}

			const Core::Result<Core::Oid> baseTip = repository.ResolveReference(base);

			if (!baseTip)
			{
				if (baseTip.GetError().Code != Core::ErrorCode::NotFound)
				{
					return baseTip.GetError();
				}

				answer.Result["problem"] = "baseMissing";
				return answer;
			}

			const Core::Result<Core::Oid> mergeBase = repository.MergeBase(tip.GetValue(), baseTip.GetValue());

			if (!mergeBase)
			{
				if (mergeBase.GetError().Code != Core::ErrorCode::NotFound)
				{
					return mergeBase.GetError();
				}

				answer.Result["problem"] = "noCommonAncestor";
				return answer;
			}

			answer.Result["mergeBase"] = mergeBase.GetValue().ToHex();
			answer.Result["tip"] = tip.GetValue().ToHex();

			Core::LogRequest commitsSinceMergeBase;
			commitsSinceMergeBase.Ref = branch;
			commitsSinceMergeBase.Exclude = mergeBase.GetValue();
			commitsSinceMergeBase.MaxCount = kMostCommitsListedInRange;
			const Core::Result<Core::LogPage> page = repository.QueryLog(commitsSinceMergeBase);

			if (!page)
			{
				return page.GetError();
			}

			answer.Result["complete"] = page.GetValue().Complete;
			answer.Payload = Core::ToJsonList(page.GetValue().Commits).dump(-1, ' ', false, json::error_handler_t::replace);
			return answer;
		}

		Core::Result<GitAnswer> ReadBlobs(Core::Repository& repository, const json& arguments)
		{
			GitAnswer answer;
			answer.Result["sides"] = json::array();

			for (const json& read : arguments.value("reads", json::array()))
			{
				const std::string path = read.is_object() ? read.value("path", std::string{}) : std::string{};
				const std::optional<Core::Oid> revision = read.is_object() ? OidOf(read, "revision") : std::nullopt;

				if (path.empty())
				{
					AddSide(answer, std::nullopt);
					continue;
				}

				if (!revision)
				{
					return Core::Error{Core::ErrorCode::InvalidArgument, "a read names no revision"};
				}

				const Core::Result<std::optional<std::string>> side = PresentOrAbsent(repository.ReadBlob(path, *revision));

				if (!side)
				{
					return side.GetError();
				}

				AddSide(answer, side.GetValue());
			}

			return answer;
		}

		// Read where the root is the worktree itself (reach exactly).
		Core::Result<GitAnswer> Activity(Core::Repository& repository, const json&)
		{
			GitAnswer answer;

			if (const std::optional<std::int64_t> index = ModifiedAt(repository.GitDirectory() / "index"))
			{
				answer.Result["index"] = *index;
			}

			const Core::Result<std::chrono::sys_seconds> reflog = repository.HeadReflogTime();

			if (reflog)
			{
				answer.Result["reflog"] = SecondsOf(reflog.GetValue());
			}
			else if (reflog.GetError().Code != Core::ErrorCode::NotFound)
			{
				return reflog.GetError();
			}

			const Core::Result<std::chrono::sys_seconds> headTime = repository.HeadCommitTime();

			if (headTime)
			{
				answer.Result["headCommit"] = SecondsOf(headTime.GetValue());
			}
			else if (headTime.GetError().Code != Core::ErrorCode::NotFound)
			{
				return headTime.GetError();
			}

			return answer;
		}

		Core::Result<GitAnswer> CountUncommitted(Core::Repository& repository, const json&)
		{
			const Core::Result<Core::WorkingTreeStatus> status = repository.QueryStatus(Core::SubmoduleWalk::Skipped);

			if (!status)
			{
				return status.GetError();
			}

			GitAnswer answer{.Result = {{"changed", status.GetValue().Entries.size()}}};
			const std::filesystem::path workingDirectory = repository.GetWorkingDirectory();
			std::optional<std::int64_t> newest;

			for (const Core::FileStatus& entry : status.GetValue().Entries)
			{
				const std::optional<std::int64_t> written = ModifiedAt(workingDirectory / entry.Path);

				if (written && (!newest || *written > *newest))
				{
					newest = written;
				}
			}

			if (newest)
			{
				answer.Result["newest"] = *newest;
			}

			return answer;
		}

		// The viewed blob and the current version: a tip's blob, or the worktree's file. A current
		// version that is not there is a failed read: only the viewed blob may be not found.
		Core::Result<GitAnswer> SinceViewed(Core::Repository& repository, const json& arguments)
		{
			const Core::Result<Core::Oid> viewedId = RequiredOid(arguments, "viewed");

			if (!viewedId)
			{
				return viewedId.GetError();
			}

			Core::Result<std::string> viewed = repository.ReadBlobById(viewedId.GetValue());

			if (!viewed)
			{
				return viewed.GetError();
			}

			const std::filesystem::path path{arguments.value("path", std::string{})};
			Core::Result<std::string> current = Core::Error{Core::ErrorCode::InvalidArgument, "a since-viewed request names neither a tip nor a worktree"};

			if (const std::optional<Core::Oid> tip = OidOf(arguments, "tip"))
			{
				current = repository.ReadBlob(path, *tip);
			}
			else if (const auto worktree = arguments.find("worktree"); worktree != arguments.end() && worktree->is_string())
			{
				const Core::Result<Core::Repository> opened = Core::Repository::OpenExactly(worktree->get<std::string>());
				current = opened ? opened.GetValue().ReadWorkingTreeFile(path) : Core::Result<std::string>{opened.GetError()};
			}

			if (!current)
			{
				Core::Error failure = current.GetError();

				if (failure.Code == Core::ErrorCode::NotFound)
				{
					failure.Code = Core::ErrorCode::IoFailure;
				}

				return failure;
			}

			GitAnswer answer;
			AddSide(answer, std::move(viewed.GetValue()));
			AddSide(answer, std::move(current.GetValue()));
			return answer;
		}
	}

	void AddReviewHandlers(GitHandlers& handlers, std::shared_ptr<RepositoryCache> cache)
	{
		handlers["git.blameAtHead"] = [cache](const Core::AgentRequest& request) -> Core::Result<GitAnswer> {
			const Core::Result<Asked> asked = AskedOf(request.Arguments);

			if (!asked)
			{
				return asked.GetError();
			}

			// No repository, or one that cannot be opened, leaves the file untracked, as the host's port answers.
			const Core::Result<Core::Repository*> repository = cache->Get(asked.GetValue().Root, asked.GetValue().Exactly);
			return repository ? BlameAtHead(*repository.GetValue(), request.Arguments) : Core::Result<GitAnswer>{GitAnswer{.Result = {{"trackable", false}}}};
		};

		handlers["git.lineHistory"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::LineHistoryRequest> request = Core::LineHistoryRequestFromJson(arguments.value("request", json::object()));

			if (!request)
			{
				return request.GetError();
			}

			const Core::Result<Core::LineHistory> history = repository.QueryLineHistory(request.GetValue());
			return history ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(history.GetValue()))} : Core::Result<GitAnswer>{history.GetError()};
		});

		handlers["git.resolveRange"] = OnRepository(cache, ResolveRange);
		handlers["git.readBlobs"] = OnRepository(cache, ReadBlobs);

		handlers["git.worktrees"] = OnRepository(cache, [](Core::Repository& repository, const json&) -> Core::Result<GitAnswer> {
			const Core::Result<std::vector<Core::WorktreeEntry>> worktrees = repository.QueryWorktrees();
			return worktrees ? Core::Result<GitAnswer>{AsPayload(Core::ToJsonList(worktrees.GetValue()))} : Core::Result<GitAnswer>{worktrees.GetError()};
		});

		handlers["git.activity"] = OnRepository(cache, Activity);
		handlers["git.countUncommitted"] = OnRepository(cache, CountUncommitted);
		handlers["git.sinceViewed"] = OnRepository(cache, SinceViewed);
	}
}

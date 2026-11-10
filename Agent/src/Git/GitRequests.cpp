#include "corepch.h"

#include "Git/GitRequests.h"

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

		// One handle per folder asked about, kept between requests: libgit2 reads the index and
		// config once, as the host's port does with its own cache. Used on the git worker only.
		class RepositoryCache
		{
		public:
			Core::Result<Core::Repository*> Get(const std::string& root, bool exactly)
			{
				const std::string key = (exactly ? "=" : "^") + root;
				const auto cached = m_Repositories.find(key);

				if (cached != m_Repositories.end() && !HasVanished(cached->second))
				{
					return &cached->second;
				}

				if (cached != m_Repositories.end())
				{
					m_Repositories.erase(cached);
				}

				Core::Result<Core::Repository> opened = exactly ? Core::Repository::OpenExactly(root) : Core::Repository::Open(root);

				if (!opened)
				{
					return opened.GetError();
				}

				return &m_Repositories.insert_or_assign(key, std::move(opened.GetValue())).first->second;
			}

			void Forget(const std::string& root)
			{
				m_Repositories.erase("=" + root);
				m_Repositories.erase("^" + root);
			}

		private:
			// libgit2 keeps answering from a handle whose .git was deleted.
			static bool HasVanished(const Core::Repository& repository)
			{
				const std::filesystem::path workingDirectory = repository.GetWorkingDirectory();

				if (workingDirectory.empty())
				{
					return false;
				}

				std::error_code error;
				return !std::filesystem::exists(workingDirectory / ".git", error) && !error;
			}

			std::map<std::string, Core::Repository> m_Repositories;
		};

		struct Asked
		{
			std::string Root;
			bool Exactly = false;
		};

		Core::Result<Asked> AskedOf(const json& arguments)
		{
			const auto root = arguments.find("root");

			if (root == arguments.end() || !root->is_string() || root->get<std::string>().empty() || root->get<std::string>().front() != '/')
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "the request has no absolute \"root\""};
			}

			return Asked{.Root = root->get<std::string>(), .Exactly = arguments.value("reach", std::string{"up"}) == "exactly"};
		}

		std::vector<std::filesystem::path> PathsOf(const json& arguments)
		{
			std::vector<std::filesystem::path> paths;

			if (const auto listed = arguments.find("paths"); listed != arguments.end() && listed->is_array())
			{
				for (const json& path : *listed)
				{
					if (path.is_string())
					{
						paths.emplace_back(path.get<std::string>());
					}
				}
			}

			return paths;
		}

		GitAnswer AsPayload(const json& result)
		{
			return GitAnswer{.Result = json::object(), .Payload = result.dump(-1, ' ', false, json::error_handler_t::replace)};
		}

		using RepositoryHandler = std::function<Core::Result<GitAnswer>(Core::Repository& repository, const json& arguments)>;

		// Opens the request's repository through the cache, then answers with handler.
		GitHandler OnRepository(std::shared_ptr<RepositoryCache> cache, RepositoryHandler handler)
		{
			return [cache = std::move(cache), handler = std::move(handler)](const Core::AgentRequest& request) -> Core::Result<GitAnswer> {
				const Core::Result<Asked> asked = AskedOf(request.Arguments);

				if (!asked)
				{
					return asked.GetError();
				}

				const Core::Result<Core::Repository*> repository = cache->Get(asked.GetValue().Root, asked.GetValue().Exactly);

				if (!repository)
				{
					return repository.GetError();
				}

				return handler(*repository.GetValue(), request.Arguments);
			};
		}

		Core::Result<GitAnswer> Done(const Core::Result<void>& outcome)
		{
			if (!outcome)
			{
				return outcome.GetError();
			}

			return GitAnswer{};
		}

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

		// The working directory in the spelling of the root Graphite asked with: libgit2 resolves
		// symbolic links (~/code -> /data/code), and the host names files by the folder it opened,
		// so a resolved spelling would open the same file under another path. Empty for a bare
		// repository, which has no working directory.
		std::string SpelledLikeRoot(const std::filesystem::path& workingDirectory, const std::string& root)
		{
			if (workingDirectory.empty())
			{
				return {};
			}

			const std::filesystem::path resolved = Core::NormalizeDirectory(workingDirectory);
			std::error_code error;
			const std::filesystem::path resolvedRoot = std::filesystem::weakly_canonical(root, error);

			if (error)
			{
				return resolved.string(); // not a path conversion: Linux paths are their bytes
			}

			// The repository sits at or above the root: climb the asked spelling as far as the
			// resolved one climbs.
			const std::filesystem::path below = resolvedRoot.lexically_relative(resolved);

			if (below.empty() || *below.begin() == "..")
			{
				return resolved.string(); // not a path conversion
			}

			std::filesystem::path spelled = Core::NormalizeDirectory(root);

			for (const std::filesystem::path& part : below)
			{
				if (part != ".")
				{
					spelled = spelled.parent_path();
				}
			}

			return spelled.string(); // not a path conversion
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

			if (!read)
			{
				if (read.GetError().Code == Core::ErrorCode::NotFound)
				{
					return std::optional<std::string>{};
				}

				return read.GetError();
			}

			return std::optional<std::string>{std::move(read.GetValue())};
		}

		// Sides one after another in the payload, each listed with its size or as absent.
		Core::Result<GitAnswer> ReadSides(Core::Repository& repository, const std::vector<std::pair<Side, std::filesystem::path>>& reads)
		{
			GitAnswer answer;
			json sides = json::array();

			for (const auto& [side, path] : reads)
			{
				const Core::Result<std::optional<std::string>> read = ReadSide(repository, side, path);

				if (!read)
				{
					return read.GetError();
				}

				if (read.GetValue())
				{
					sides.push_back({{"present", true}, {"size", read.GetValue()->size()}});
					answer.Payload += *read.GetValue();
				}
				else
				{
					sides.push_back({{"present", false}, {"size", 0}});
				}
			}

			answer.Result = {{"sides", std::move(sides)}};
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

		return handlers;
	}
}

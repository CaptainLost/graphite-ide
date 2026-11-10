#include "corepch.h"

#include "Git/RepositoryRequests.h"

#include "Core/FileSystem/FileSystem.h"

#include <system_error>
#include <utility>

namespace Agent {

	using nlohmann::json;

	namespace {

		// libgit2 keeps answering from a handle whose .git was deleted.
		bool HasVanished(const Core::Repository& repository)
		{
			const std::filesystem::path workingDirectory = repository.GetWorkingDirectory();

			if (workingDirectory.empty())
			{
				return false;
			}

			std::error_code error;
			return !std::filesystem::exists(workingDirectory / ".git", error) && !error;
		}
	}

	Core::Result<Core::Repository*> RepositoryCache::Get(const std::string& root, bool exactly)
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

	void RepositoryCache::Forget(const std::string& root)
	{
		m_Repositories.erase("=" + root);
		m_Repositories.erase("^" + root);
	}

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

	Core::Result<GitAnswer> Done(const Core::Result<void>& outcome)
	{
		if (!outcome)
		{
			return outcome.GetError();
		}

		return GitAnswer{};
	}

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

	void AddSide(GitAnswer& answer, const std::optional<std::string>& side)
	{
		if (!answer.Result.contains("sides"))
		{
			answer.Result["sides"] = json::array();
		}

		if (side)
		{
			answer.Result["sides"].push_back({{"present", true}, {"size", side->size()}});
			answer.Payload += *side;
		}
		else
		{
			answer.Result["sides"].push_back({{"present", false}, {"size", 0}});
		}
	}

	Core::Result<std::optional<std::string>> PresentOrAbsent(Core::Result<std::string> read)
	{
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
}

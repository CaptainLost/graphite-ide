#include "corepch.h"

#include "Git/ContentRequests.h"

#include "Git/RepositoryRequests.h"

#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/PatchMatch.h"
#include "Core/Remote/GitCodec.h"

#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Agent {

	using nlohmann::json;

	namespace {

		Core::Result<Core::Oid> RequiredOid(const json& arguments, const char* name)
		{
			const auto found = arguments.find(name);
			const std::optional<Core::Oid> id =
				found != arguments.end() && found->is_string() ? Core::Oid::FromHex(found->get<std::string>()) : std::nullopt;

			if (!id)
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, std::format("the request has no commit id \"{}\"", name)};
			}

			return *id;
		}

		// One cache of patch ids per repository (its common directory), used on the git worker only.
		class PatchIdCaches
		{
		public:
			Core::PatchIdCache& Of(const Core::Repository& repository)
			{
				std::unique_ptr<Core::PatchIdCache>& cache = m_Caches[repository.CommonDirectory().string()]; // not a path conversion

				if (!cache)
				{
					cache = std::make_unique<Core::PatchIdCache>();
				}

				return *cache;
			}

		private:
			std::map<std::string, std::unique_ptr<Core::PatchIdCache>> m_Caches;
		};
	}

	void AddContentHandlers(GitHandlers& handlers)
	{
		auto cache = std::make_shared<RepositoryCache>(Core::ObjectWrites::InMemory);
		auto patchIds = std::make_shared<PatchIdCaches>();

		handlers["git.checkContent"] = OnRepository(cache, [patchIds](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::Oid> branchTip = RequiredOid(arguments, "branchTip");
			const Core::Result<Core::Oid> lineTip = RequiredOid(arguments, "lineTip");

			if (!branchTip || !lineTip)
			{
				return branchTip ? lineTip.GetError() : branchTip.GetError();
			}

			const Core::Result<Core::ContentVerdict> verdict =
				Core::CheckMergedByContent(repository, branchTip.GetValue(), lineTip.GetValue(), patchIds->Of(repository));
			return verdict ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(verdict.GetValue()))} : Core::Result<GitAnswer>{verdict.GetError()};
		});

		handlers["git.findTwin"] = OnRepository(cache, [patchIds](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::Oid> commit = RequiredOid(arguments, "commit");
			const Core::Result<Core::Oid> lineTip = RequiredOid(arguments, "lineTip");

			if (!commit || !lineTip)
			{
				return commit ? lineTip.GetError() : commit.GetError();
			}

			const Core::Result<Core::PatchTwinSearch> search =
				repository.FindPatchTwin(commit.GetValue(), lineTip.GetValue(), arguments.value("budget", std::size_t{0}), patchIds->Of(repository));
			return search ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(search.GetValue()))} : Core::Result<GitAnswer>{search.GetError()};
		});

		handlers["git.pathsChangedBetween"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::Oid> older = RequiredOid(arguments, "older");
			const Core::Result<Core::Oid> newer = RequiredOid(arguments, "newer");

			if (!older || !newer)
			{
				return older ? newer.GetError() : older.GetError();
			}

			const Core::Result<std::vector<std::string>> paths = repository.PathsChangedBetween(older.GetValue(), newer.GetValue());
			return paths ? Core::Result<GitAnswer>{AsPayload(paths.GetValue())} : Core::Result<GitAnswer>{paths.GetError()};
		});

		// The paths come in the payload as a JSON list: a change can touch more than a header holds.
		handlers["git.treesDifferAt"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments, const std::string& payload) -> Core::Result<GitAnswer> {
			const Core::Result<Core::Oid> from = RequiredOid(arguments, "from");
			const Core::Result<Core::Oid> to = RequiredOid(arguments, "to");

			if (!from || !to)
			{
				return from ? to.GetError() : from.GetError();
			}

			const json listed = json::parse(payload, nullptr, false);

			if (!listed.is_array())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "the request's paths are not a list"};
			}

			std::vector<std::string> paths;

			for (const json& path : listed)
			{
				if (path.is_string())
				{
					paths.push_back(path.get<std::string>());
				}
			}

			const Core::Result<bool> differ = repository.TreesDifferAt(from.GetValue(), to.GetValue(), paths);
			return differ ? Core::Result<GitAnswer>{GitAnswer{.Result = {{"differ", differ.GetValue()}}}} : Core::Result<GitAnswer>{differ.GetError()};
		});
	}
}

#include "corepch.h"

#include "Git/HistoryRequests.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Tag.h"
#include "Core/Remote/GitCodec.h"
#include "Core/Remote/GraphCodec.h"

#include <format>
#include <optional>
#include <string>
#include <utility>

namespace Agent {

	using nlohmann::json;

	namespace {

		Core::Result<Core::Oid> OidArgument(const json& arguments, const char* name)
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

		Core::Result<std::string> TextArgument(const json& arguments, const char* name)
		{
			const auto found = arguments.find(name);

			if (found == arguments.end() || !found->is_string() || found->get<std::string>().empty())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, std::format("the request has no \"{}\"", name)};
			}

			return found->get<std::string>();
		}

		template<typename Value>
		Core::Result<GitAnswer> PayloadOf(const Core::Result<Value>& result)
		{
			if (!result)
			{
				return result.GetError();
			}

			return AsPayload(Core::ToJson(result.GetValue()));
		}

		template<typename Value>
		Core::Result<GitAnswer> PayloadOfList(const Core::Result<std::vector<Value>>& result)
		{
			if (!result)
			{
				return result.GetError();
			}

			return AsPayload(Core::ToJsonList(result.GetValue()));
		}

		// As the host's port reads a commit's file: the new side from the commit, or from the files
		// a stash took untracked when the commit is a stash without it; the old side from the
		// first parent, absent for a root commit.
		Core::Result<GitAnswer> CommitSides(Core::Repository& repository, const Core::Oid& commit, const std::filesystem::path& oldPath,
											const std::filesystem::path& newPath)
		{
			std::optional<std::string> newSide;

			if (!newPath.empty())
			{
				Core::Result<std::optional<std::string>> read = PresentOrAbsent(repository.ReadBlob(newPath, commit));

				if (!read)
				{
					return read.GetError();
				}

				newSide = std::move(read.GetValue());

				if (!newSide)
				{
					const Core::Result<std::optional<Core::Oid>> untracked = repository.UntrackedOfStash(commit);

					if (!untracked)
					{
						return untracked.GetError();
					}

					if (untracked.GetValue())
					{
						Core::Result<std::optional<std::string>> stashed = PresentOrAbsent(repository.ReadBlob(newPath, *untracked.GetValue()));

						if (!stashed)
						{
							return stashed.GetError();
						}

						newSide = std::move(stashed.GetValue());
					}
				}
			}

			const Core::Result<std::optional<Core::Oid>> parent = repository.FirstParentOf(commit);

			if (!parent)
			{
				return parent.GetError();
			}

			std::optional<std::string> oldSide;

			if (parent.GetValue() && !oldPath.empty())
			{
				Core::Result<std::optional<std::string>> read = PresentOrAbsent(repository.ReadBlob(oldPath, *parent.GetValue()));

				if (!read)
				{
					return read.GetError();
				}

				oldSide = std::move(read.GetValue());
			}

			GitAnswer answer;
			AddSide(answer, newSide);
			AddSide(answer, oldSide);
			return answer;
		}

		Core::Result<GitAnswer> CountCommits(Core::Repository& repository, const json& arguments)
		{
			const auto ref = arguments.find("ref");
			const Core::Result<Core::Oid> tip = ref != arguments.end() && ref->is_string() ? repository.ResolveReference(ref->get<std::string>())
																						   : repository.HeadCommit();

			if (!tip)
			{
				return tip.GetError();
			}

			GitAnswer answer{.Result = {{"tip", tip.GetValue().ToHex()}}};
			const Core::Result<Core::Oid> known = OidArgument(arguments, "knownTip");

			// The host keeps the count of a tip it has seen: an unchanged tip is not counted again.
			if (known && known.GetValue() == tip.GetValue())
			{
				return answer;
			}

			const Core::FollowedParents parents = arguments.value("firstParent", false) ? Core::FollowedParents::FirstOnly : Core::FollowedParents::All;
			const Core::Result<std::size_t> count = repository.CountCommits(tip.GetValue(), parents);

			if (!count)
			{
				return count.GetError();
			}

			answer.Result["count"] = count.GetValue();
			return answer;
		}
	}

	void AddHistoryHandlers(GitHandlers& handlers, std::shared_ptr<RepositoryCache> cache)
	{
		// A tag object never changes, so what it peels to is kept for the agent's life.
		auto tags = std::make_shared<Core::TagCache>();

		handlers["git.branches"] = OnRepository(cache, [](Core::Repository& repository, const json&) { return PayloadOfList(repository.QueryBranches()); });

		handlers["git.tags"] = OnRepository(cache, [tags](Core::Repository& repository, const json&) { return PayloadOfList(repository.QueryTags(*tags)); });

		handlers["git.remotes"] = OnRepository(cache, [](Core::Repository& repository, const json&) { return PayloadOfList(repository.QueryRemotes()); });

		handlers["git.stashes"] = OnRepository(cache, [](Core::Repository& repository, const json&) { return PayloadOfList(repository.QueryStashes()); });

		handlers["git.ownAuthors"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::OwnAuthorsRequest> request = Core::OwnAuthorsRequestFromJson(arguments.value("request", json::object()));

			if (!request)
			{
				return request.GetError();
			}

			return PayloadOfList(repository.QueryOwnAuthors(request.GetValue()));
		});

		handlers["git.userEmail"] = OnRepository(cache, [](Core::Repository& repository, const json&) -> Core::Result<GitAnswer> {
			const Core::Result<std::optional<std::string>> email = repository.ReadUserEmail();

			if (!email)
			{
				return email.GetError();
			}

			return GitAnswer{.Result = {{"email", email.GetValue() ? json(*email.GetValue()) : json(nullptr)}}};
		});

		handlers["git.log"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::LogRequest> request = Core::LogRequestFromJson(arguments.value("request", json::object()));

			if (!request)
			{
				return request.GetError();
			}

			return PayloadOf(repository.QueryLog(request.GetValue()));
		});

		handlers["git.countCommits"] = OnRepository(cache, CountCommits);

		handlers["git.commitSides"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::Oid> commit = OidArgument(arguments, "commit");

			if (!commit)
			{
				return commit.GetError();
			}

			return CommitSides(repository, commit.GetValue(), arguments.value("oldPath", std::string{}), arguments.value("newPath", std::string{}));
		});

		handlers["git.createBranch"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<std::string> name = TextArgument(arguments, "name");
			const Core::Result<Core::Oid> from = OidArgument(arguments, "from");

			if (!name || !from)
			{
				return name ? from.GetError() : name.GetError();
			}

			return Done(repository.CreateBranch(name.GetValue(), from.GetValue()));
		});

		handlers["git.renameBranch"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<std::string> from = TextArgument(arguments, "from");
			const Core::Result<std::string> to = TextArgument(arguments, "to");

			if (!from || !to)
			{
				return from ? to.GetError() : from.GetError();
			}

			return Done(repository.RenameBranch(from.GetValue(), to.GetValue()));
		});

		handlers["git.countUnmerged"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<std::string> name = TextArgument(arguments, "name");

			if (!name)
			{
				return name.GetError();
			}

			const Core::Result<std::size_t> count = repository.CountUnmergedCommits(name.GetValue());
			return count ? Core::Result<GitAnswer>{GitAnswer{.Result = {{"count", count.GetValue()}}}} : Core::Result<GitAnswer>{count.GetError()};
		});

		handlers["git.deleteBranch"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<std::string> name = TextArgument(arguments, "name");
			return name ? Done(repository.DeleteBranch(name.GetValue())) : Core::Result<GitAnswer>{name.GetError()};
		});

		handlers["git.stashSave"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::UntrackedFiles untracked = arguments.value("untracked", false) ? Core::UntrackedFiles::Included : Core::UntrackedFiles::Excluded;
			const Core::Result<bool> saved = repository.StashSave(arguments.value("message", std::string{}), untracked);
			return saved ? Core::Result<GitAnswer>{GitAnswer{.Result = {{"saved", saved.GetValue()}}}} : Core::Result<GitAnswer>{saved.GetError()};
		});

		const auto onStash = [&](const char* operation, Core::Result<void> (Core::Repository::*act)(const Core::Oid&)) {
			handlers[operation] = OnRepository(cache, [act](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
				const Core::Result<Core::Oid> id = OidArgument(arguments, "id");
				return id ? Done((repository.*act)(id.GetValue())) : Core::Result<GitAnswer>{id.GetError()};
			});
		};

		onStash("git.stashApply", &Core::Repository::StashApply);
		onStash("git.stashPop", &Core::Repository::StashPop);
		onStash("git.stashDrop", &Core::Repository::StashDrop);

		handlers["git.planCheckout"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::CheckoutTarget> target = Core::CheckoutTargetFromJson(arguments.value("target", json::object()));
			return target ? PayloadOf(repository.PlanCheckout(target.GetValue())) : Core::Result<GitAnswer>{target.GetError()};
		});

		// Answered even when the checkout failed: a failure after writing began still names the
		// paths it may have changed.
		handlers["git.checkout"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments) -> Core::Result<GitAnswer> {
			const Core::Result<Core::CheckoutTarget> target = Core::CheckoutTargetFromJson(arguments.value("target", json::object()));
			return target ? Core::Result<GitAnswer>{AsPayload(Core::ToJson(repository.Checkout(target.GetValue())))} : Core::Result<GitAnswer>{target.GetError()};
		});

		// The lines graph's reads (stage 3b-lines): the commits in a compact binary payload.
		// The ids come in the payload, tips first: a graph's known heads can pass the header's limit.
		handlers["git.graphCommits"] = OnRepository(cache, [](Core::Repository& repository, const json& arguments, const std::string& payload) -> Core::Result<GitAnswer> {
			Core::Result<std::vector<Core::Oid>> ids = Core::DecodeCommitIds(payload);
			const std::size_t tipCount = arguments.value("tipCount", std::size_t{0});

			if (!ids || tipCount > ids.GetValue().size())
			{
				return ids ? Core::Error{Core::ErrorCode::InvalidArgument, "the request names more tips than ids"} : ids.GetError();
			}

			std::vector<Core::Oid>& listed = ids.GetValue();
			Core::GraphReadRequest request;
			request.Tips.assign(listed.begin(), listed.begin() + static_cast<std::ptrdiff_t>(tipCount));
			request.Known.assign(listed.begin() + static_cast<std::ptrdiff_t>(tipCount), listed.end());

			const Core::Result<std::vector<Core::GraphCommit>> commits = repository.ReadGraphCommits(request);

			if (!commits)
			{
				return commits.GetError();
			}

			return GitAnswer{.Result = {{"count", commits.GetValue().size()}}, .Payload = Core::EncodeGraphCommits(commits.GetValue())};
		});

		handlers["git.holdsCommits"] = OnRepository(cache, [](Core::Repository& repository, const json&, const std::string& payload) -> Core::Result<GitAnswer> {
			const Core::Result<std::vector<Core::Oid>> ids = Core::DecodeCommitIds(payload);

			if (!ids)
			{
				return ids.GetError();
			}

			json held = json::array();

			for (const Core::Oid& id : ids.GetValue())
			{
				const Core::Result<bool> holds = repository.HoldsCommit(id);

				if (!holds)
				{
					return holds.GetError();
				}

				held.push_back(holds.GetValue());
			}

			return GitAnswer{.Result = {{"held", std::move(held)}}};
		});

		handlers["git.shallowBoundary"] = OnRepository(cache, [](Core::Repository& repository, const json&) -> Core::Result<GitAnswer> {
			json ids = json::array();

			for (const Core::Oid& id : repository.ShallowBoundary())
			{
				ids.push_back(id.ToHex());
			}

			return GitAnswer{.Result = {{"ids", std::move(ids)}}};
		});

		handlers["git.commonDirectory"] = OnRepository(cache, [](Core::Repository& repository, const json&) -> Core::Result<GitAnswer> {
			return GitAnswer{.Result = {{"path", Core::NormalizeDirectory(repository.CommonDirectory()).string()}}}; // not a path conversion
		});
	}
}

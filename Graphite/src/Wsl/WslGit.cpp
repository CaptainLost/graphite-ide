#include "Wsl/WslGit.h"

#include "Wsl/WslAgents.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Remote/GitCodec.h"
#include "Core/Remote/GraphCodec.h"

#include <format>
#include <functional>
#include <set>

namespace Graphite {

	namespace {

		// The agent runs git requests one at a time, so even a small one may wait behind a status of a
		// huge repository: every request gets the long wait, and a write that runs out of it may
		// still land (see Ask).
		constexpr std::chrono::seconds kLongRequest{180};

		// How long a request waits for the distro's agent to start.
		constexpr std::chrono::seconds kStartWait{30};

		// The Explorer's questions run on the agent's quick lane, behind nothing slow.
		constexpr std::chrono::seconds kQuickRequest{2};

		// What only reads: one that runs out of time changed nothing. Any other operation may still
		// land after Graphite stopped waiting.
		const std::set<std::string, std::less<>> kReads{
			"git.status",	  "git.untrackedIn",  "git.workingDirectory", "git.head",		 "git.diff",		 "git.readSides",
			"git.bases",	  "git.branches",	  "git.tags",			  "git.ownAuthors",	 "git.userEmail",	 "git.remotes",
			"git.log",		  "git.countCommits", "git.commitSides",	  "git.countUnmerged", "git.stashes",		 "git.planCheckout",
			"git.commonDirectory", "git.ignoreSetup", "git.ignored", "git.graphCommits", "git.holdsCommits", "git.shallowBoundary",
			"git.blameAtHead", "git.lineHistory", "git.resolveRange", "git.readBlobs", "git.worktrees", "git.activity", "git.countUncommitted",
			"git.sinceViewed",
		};

		Core::Result<std::optional<Core::Oid>> OidOf(const nlohmann::json& result, const char* name)
		{
			const auto found = result.find(name);

			if (found == result.end() || found->is_null())
			{
				return std::optional<Core::Oid>{};
			}

			const std::optional<Core::Oid> id = found->is_string() ? Core::Oid::FromHex(found->get<std::string>()) : std::nullopt;

			if (!id)
			{
				return Core::Error{Core::ErrorCode::IoFailure, std::format("the WSL agent's \"{}\" is not a commit id", name)};
			}

			return std::optional<Core::Oid>{*id};
		}

		template<typename Value>
		Core::Result<Value> Decoded(const Core::Result<nlohmann::json>& payload, Core::Result<Value> (*decode)(const nlohmann::json&))
		{
			return payload ? decode(payload.GetValue()) : Core::Result<Value>{payload.GetError()};
		}

		std::string_view SideName(WslGit::Side side)
		{
			switch (side)
			{
			case WslGit::Side::Head:
				return "head";
			case WslGit::Side::Index:
				return "index";
			case WslGit::Side::WorkTree:
				return "workTree";
			}

			return "head";
		}

		Core::Result<nlohmann::json> ParsedPayload(const std::string& payload)
		{
			nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false);

			if (parsed.is_discarded())
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent answered with something that is not JSON"};
			}

			return parsed;
		}

		// The agent's sides answer: sizes in the result, the bytes one after another in the payload.
		Core::Result<std::vector<std::optional<std::string>>> SplitSides(const nlohmann::json& result, const std::string& payload)
		{
			std::vector<std::optional<std::string>> sides;
			std::size_t offset = 0;
			const auto listed = result.find("sides");

			if (listed == result.end() || !listed->is_array())
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's answer lists no sides"};
			}

			for (const nlohmann::json& side : *listed)
			{
				if (!side.value("present", false))
				{
					sides.emplace_back();
					continue;
				}

				const std::size_t size = side.value("size", std::size_t{0});

				if (offset + size > payload.size())
				{
					return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's sides are shorter than it says"};
				}

				sides.emplace_back(payload.substr(offset, size));
				offset += size;
			}

			return sides;
		}

		nlohmann::json PathsJson(const std::vector<std::filesystem::path>& paths)
		{
			nlohmann::json listed = nlohmann::json::array();

			for (const std::filesystem::path& path : paths)
			{
				listed.push_back(Core::ToGenericUtf8(path));
			}

			return listed;
		}
	}

	WslGit::WslGit(WslAgents& agents, Core::WslSharePath root, Reach reach) : m_Agents(agents), m_Root(std::move(root)), m_Reach(reach) {}

	Core::Result<WslGit::Answer> WslGit::Ask(const std::string& operation, nlohmann::json arguments, std::chrono::milliseconds timeout,
											 std::chrono::milliseconds startWait, std::string payload)
	{
		const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = m_Agents.Get(m_Root.Distro, startWait);

		if (!connection)
		{
			return connection.GetError();
		}

		arguments["root"] = m_Root.LinuxPath;
		arguments["reach"] = m_Reach == Reach::Exactly ? "exactly" : "up";

		Core::Result<Core::AgentReply> reply = connection.GetValue()->SendAndWait(operation, std::move(arguments), std::move(payload), timeout);

		if (!reply)
		{
			// The agent still has the request: a change may land after Graphite stopped waiting.
			if (reply.GetError().Code == Core::ErrorCode::Cancelled && !kReads.contains(operation))
			{
				return Core::Error{Core::ErrorCode::Cancelled,
								   std::format("the WSL agent did not answer in time; the change may still be made, so refresh before trying again")};
			}

			return reply.GetError();
		}

		return Answer{.Result = std::move(reply.GetValue().Result), .Payload = std::move(reply.GetValue().Payload)};
	}

	Core::Result<WslGit::Answer> WslGit::Ask(const std::string& operation, nlohmann::json arguments)
	{
		return Ask(operation, std::move(arguments), kLongRequest, kStartWait);
	}

	Core::Result<void> WslGit::AskForDone(const std::string& operation, nlohmann::json arguments)
	{
		const Core::Result<Answer> answer = Ask(operation, std::move(arguments));
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}

	Core::Result<nlohmann::json> WslGit::AskForPayload(const std::string& operation, nlohmann::json arguments)
	{
		const Core::Result<Answer> answer = Ask(operation, std::move(arguments));

		if (!answer)
		{
			return answer.GetError();
		}

		return ParsedPayload(answer.GetValue().Payload);
	}

	std::filesystem::path WslGit::SharePathOf(const std::string& linuxPath) const
	{
		return Core::NormalizeDirectory(Core::ToWslSharePath(Core::WslSharePath{m_Root.Distro, linuxPath}));
	}

	Core::Result<void> WslGit::AskForPaths(const std::string& operation, const std::vector<std::filesystem::path>& paths)
	{
		const Core::Result<Answer> answer = Ask(operation, {{"paths", PathsJson(paths)}});
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}

	Core::Result<Core::WorkingTreeStatus> WslGit::QueryStatus()
	{
		const Core::Result<Answer> answer = Ask("git.status", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::WorkingTreeStatusFromJson(parsed.GetValue()) : Core::Result<Core::WorkingTreeStatus>{parsed.GetError()};
	}

	Core::Result<Core::WorkingTreeStatus> WslGit::QueryUntrackedIn(const std::filesystem::path& directory)
	{
		const Core::Result<Answer> answer = Ask("git.untrackedIn", {{"directory", Core::ToGenericUtf8(directory)}});

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::WorkingTreeStatusFromJson(parsed.GetValue()) : Core::Result<Core::WorkingTreeStatus>{parsed.GetError()};
	}

	Core::Result<std::filesystem::path> WslGit::GetWorkingDirectory()
	{
		const Core::Result<Answer> answer = Ask("git.workingDirectory", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const auto path = answer.GetValue().Result.find("path");

		if (path == answer.GetValue().Result.end() || !path->is_string())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent named no working directory"};
		}

		// A bare repository has none, as a local one answers.
		if (path->get<std::string>().empty())
		{
			return std::filesystem::path{};
		}

		return Core::NormalizeDirectory(Core::ToWslSharePath(Core::WslSharePath{m_Root.Distro, path->get<std::string>()}));
	}

	Core::Result<Core::HeadState> WslGit::QueryHead()
	{
		const Core::Result<Answer> answer = Ask("git.head", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::HeadStateFromJson(parsed.GetValue()) : Core::Result<Core::HeadState>{parsed.GetError()};
	}

	Core::Result<Core::Diff> WslGit::QueryDiff(const Core::DiffRequest& request)
	{
		const Core::Result<Answer> answer = Ask("git.diff", {{"request", Core::ToJson(request)}});

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::DiffFromJson(parsed.GetValue()) : Core::Result<Core::Diff>{parsed.GetError()};
	}

	Core::Result<std::vector<std::optional<std::string>>> WslGit::ReadSides(const std::vector<std::pair<Side, std::filesystem::path>>& reads)
	{
		nlohmann::json listed = nlohmann::json::array();

		for (const auto& [side, path] : reads)
		{
			listed.push_back({{"side", SideName(side)}, {"path", Core::ToGenericUtf8(path)}});
		}

		const Core::Result<Answer> answer = Ask("git.readSides", {{"reads", std::move(listed)}});

		if (!answer)
		{
			return answer.GetError();
		}

		return SplitSides(answer.GetValue().Result, answer.GetValue().Payload);
	}

	Core::Result<WslGit::Bases> WslGit::ReadBases(const std::filesystem::path& absolutePath)
	{
		const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(absolutePath);

		// A file in another distro is not this repository's, whatever its Linux path.
		if (!place || !Core::EqualsIgnoringCase(place->Distro, m_Root.Distro))
		{
			return Bases{};
		}

		const Core::Result<Answer> answer = Ask("git.bases", {{"path", place->LinuxPath}});

		if (!answer)
		{
			return answer.GetError();
		}

		if (!answer.GetValue().Result.value("trackable", false))
		{
			return Bases{};
		}

		const Core::Result<std::vector<std::optional<std::string>>> sides = SplitSides(answer.GetValue().Result, answer.GetValue().Payload);

		if (!sides || sides.GetValue().size() != 2)
		{
			return sides ? Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's bases are not HEAD and the index"} : sides.GetError();
		}

		return Bases{.Trackable = true, .Head = sides.GetValue()[0], .Index = sides.GetValue()[1]};
	}

	Core::Result<void> WslGit::Stage(const std::vector<std::filesystem::path>& paths)
	{
		return AskForPaths("git.stage", paths);
	}

	Core::Result<void> WslGit::Unstage(const std::vector<std::filesystem::path>& paths)
	{
		return AskForPaths("git.unstage", paths);
	}

	Core::Result<void> WslGit::DiscardWorkTree(const std::vector<std::filesystem::path>& paths)
	{
		return AskForPaths("git.discard", paths);
	}

	Core::Result<void> WslGit::DeleteUntracked(const std::vector<std::filesystem::path>& paths)
	{
		return AskForPaths("git.deleteUntracked", paths);
	}

	Core::Result<void> WslGit::DiscardAll(Core::UntrackedFiles untrackedFiles)
	{
		const Core::Result<Answer> answer =
			Ask("git.discardAll", {{"untracked", untrackedFiles == Core::UntrackedFiles::Included}});
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}

	Core::Result<Core::Oid> WslGit::Commit(const Core::CommitRequest& request)
	{
		const Core::Result<Answer> answer = Ask("git.commit", {{"request", Core::ToJson(request)}});

		if (!answer)
		{
			return answer.GetError();
		}

		const std::optional<Core::Oid> oid = Core::Oid::FromHex(answer.GetValue().Result.value("oid", std::string{}));

		if (!oid)
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's commit names no commit"};
		}

		return *oid;
	}

	Core::Result<void> WslGit::Init()
	{
		const Core::Result<Answer> answer = Ask("git.init", nlohmann::json::object());
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}

	Core::Result<std::vector<Core::Branch>> WslGit::QueryBranches()
	{
		return Decoded(AskForPayload("git.branches", nlohmann::json::object()), &Core::BranchesFromJson);
	}

	Core::Result<std::vector<Core::Tag>> WslGit::QueryTags()
	{
		return Decoded(AskForPayload("git.tags", nlohmann::json::object()), &Core::TagsFromJson);
	}

	Core::Result<std::vector<Core::TipAuthors>> WslGit::QueryOwnAuthors(const Core::OwnAuthorsRequest& request)
	{
		return Decoded(AskForPayload("git.ownAuthors", {{"request", Core::ToJson(request)}}), &Core::TipAuthorsFromJson);
	}

	Core::Result<std::optional<std::string>> WslGit::ReadUserEmail()
	{
		const Core::Result<Answer> answer = Ask("git.userEmail", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const auto email = answer.GetValue().Result.find("email");

		if (email == answer.GetValue().Result.end() || email->is_null())
		{
			return std::optional<std::string>{};
		}

		if (!email->is_string())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's email is not text"};
		}

		return std::optional<std::string>{email->get<std::string>()};
	}

	Core::Result<std::vector<Core::Remote>> WslGit::QueryRemotes()
	{
		return Decoded(AskForPayload("git.remotes", nlohmann::json::object()), &Core::RemotesFromJson);
	}

	Core::Result<Core::LogPage> WslGit::QueryLog(const Core::LogRequest& request)
	{
		return Decoded(AskForPayload("git.log", {{"request", Core::ToJson(request)}}), &Core::LogPageFromJson);
	}

	Core::Result<std::pair<Core::Oid, std::optional<std::size_t>>> WslGit::CountCommits(const std::optional<std::string>& ref,
																						  Core::FollowedParents parents,
																						  const std::optional<Core::Oid>& knownTip)
	{
		nlohmann::json arguments{{"firstParent", parents == Core::FollowedParents::FirstOnly}};

		if (ref)
		{
			arguments["ref"] = *ref;
		}

		if (knownTip)
		{
			arguments["knownTip"] = knownTip->ToHex();
		}

		const Core::Result<Answer> answer = Ask("git.countCommits", std::move(arguments));

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<std::optional<Core::Oid>> tip = OidOf(answer.GetValue().Result, "tip");

		if (!tip || !tip.GetValue())
		{
			return tip ? Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's count names no tip"} : tip.GetError();
		}

		const auto count = answer.GetValue().Result.find("count");
		const std::optional<std::size_t> counted =
			count != answer.GetValue().Result.end() && count->is_number_unsigned() ? std::optional{count->get<std::size_t>()} : std::nullopt;

		if (!counted && tip.GetValue() != knownTip)
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent did not count a tip it had not been told of"};
		}

		return std::pair{*tip.GetValue(), counted};
	}

	Core::Result<std::pair<std::optional<std::string>, std::optional<std::string>>> WslGit::ReadCommitSides(const Core::Oid& commit,
																											 const std::filesystem::path& oldPath,
																											 const std::filesystem::path& newPath)
	{
		const Core::Result<Answer> answer =
			Ask("git.commitSides", {{"commit", commit.ToHex()}, {"oldPath", Core::ToGenericUtf8(oldPath)}, {"newPath", Core::ToGenericUtf8(newPath)}});

		if (!answer)
		{
			return answer.GetError();
		}

		Core::Result<std::vector<std::optional<std::string>>> sides = SplitSides(answer.GetValue().Result, answer.GetValue().Payload);

		if (!sides || sides.GetValue().size() != 2)
		{
			return sides ? Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's commit sides are not a new and an old one"} : sides.GetError();
		}

		return std::pair{std::move(sides.GetValue()[0]), std::move(sides.GetValue()[1])};
	}

	Core::Result<void> WslGit::CreateBranch(const std::string& name, const Core::Oid& from)
	{
		return AskForDone("git.createBranch", {{"name", name}, {"from", from.ToHex()}});
	}

	Core::Result<void> WslGit::RenameBranch(const std::string& from, const std::string& to)
	{
		return AskForDone("git.renameBranch", {{"from", from}, {"to", to}});
	}

	Core::Result<std::size_t> WslGit::CountUnmergedCommits(const std::string& name)
	{
		const Core::Result<Answer> answer = Ask("git.countUnmerged", {{"name", name}});

		if (!answer)
		{
			return answer.GetError();
		}

		const auto count = answer.GetValue().Result.find("count");

		if (count == answer.GetValue().Result.end() || !count->is_number_unsigned())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's answer has no count"};
		}

		return count->get<std::size_t>();
	}

	Core::Result<void> WslGit::DeleteBranch(const std::string& name)
	{
		return AskForDone("git.deleteBranch", {{"name", name}});
	}

	Core::Result<bool> WslGit::StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles)
	{
		const Core::Result<Answer> answer = Ask("git.stashSave", {{"message", message}, {"untracked", untrackedFiles == Core::UntrackedFiles::Included}});

		if (!answer)
		{
			return answer.GetError();
		}

		return answer.GetValue().Result.value("saved", false);
	}

	Core::Result<std::vector<Core::StashEntry>> WslGit::QueryStashes()
	{
		return Decoded(AskForPayload("git.stashes", nlohmann::json::object()), &Core::StashesFromJson);
	}

	Core::Result<void> WslGit::StashApply(const Core::Oid& id)
	{
		return AskForDone("git.stashApply", {{"id", id.ToHex()}});
	}

	Core::Result<void> WslGit::StashPop(const Core::Oid& id)
	{
		return AskForDone("git.stashPop", {{"id", id.ToHex()}});
	}

	Core::Result<void> WslGit::StashDrop(const Core::Oid& id)
	{
		return AskForDone("git.stashDrop", {{"id", id.ToHex()}});
	}

	Core::Result<Core::CheckoutPlan> WslGit::PlanCheckout(const Core::CheckoutTarget& target)
	{
		return Decoded(AskForPayload("git.planCheckout", {{"target", Core::ToJson(target)}}), &Core::CheckoutPlanFromJson);
	}

	Core::CheckoutOutcome WslGit::Checkout(const Core::CheckoutTarget& target)
	{
		const Core::Result<Core::CheckoutOutcome> outcome =
			Decoded(AskForPayload("git.checkout", {{"target", Core::ToJson(target)}}), &Core::CheckoutOutcomeFromJson);

		// WARNING: Changed is empty here although a checkout that ran out of time may still be
		// switching the tree in the distro; the failure says so ("may still be made"), and the
		// tree's own watch reports what it then writes.
		if (!outcome)
		{
			return Core::CheckoutOutcome{.Failure = outcome.GetError()};
		}

		return outcome.GetValue();
	}

	Core::Result<std::filesystem::path> WslGit::GetCommonDirectory()
	{
		{
			std::scoped_lock lock{m_CommonMutex};

			if (m_CommonDirectory)
			{
				return *m_CommonDirectory;
			}
		}

		const Core::Result<Answer> answer = Ask("git.commonDirectory", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const auto path = answer.GetValue().Result.find("path");

		if (path == answer.GetValue().Result.end() || !path->is_string() || path->get<std::string>().empty())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent named no common directory"};
		}

		std::filesystem::path common = SharePathOf(path->get<std::string>());
		std::scoped_lock lock{m_CommonMutex};
		m_CommonDirectory = common;
		return common;
	}

	namespace {

		std::optional<std::chrono::sys_seconds> SecondsFrom(const nlohmann::json& result, const char* name)
		{
			const auto found = result.find(name);

			if (found == result.end() || !found->is_number_integer())
			{
				return std::nullopt;
			}

			return std::chrono::sys_seconds{std::chrono::seconds{found->get<std::int64_t>()}};
		}
	}

	Core::Result<WslGit::HeadBlameRead> WslGit::BlameAtHead(const std::filesystem::path& absolutePath)
	{
		const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(absolutePath);

		if (!place || !Core::EqualsIgnoringCase(place->Distro, m_Root.Distro))
		{
			return HeadBlameRead{};
		}

		const Core::Result<Answer> answer = Ask("git.blameAtHead", {{"path", place->LinuxPath}});

		if (!answer)
		{
			return answer.GetError();
		}

		if (!answer.GetValue().Result.value("trackable", false))
		{
			return HeadBlameRead{};
		}

		const std::size_t headSize = answer.GetValue().Result.value("headSize", std::size_t{0});
		const std::string& payload = answer.GetValue().Payload;

		if (headSize > payload.size())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's blame is shorter than it says"};
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(payload.substr(headSize));
		Core::Result<Core::Blame> blame = parsed ? Core::BlameFromJson(parsed.GetValue()) : Core::Result<Core::Blame>{parsed.GetError()};

		if (!blame)
		{
			return blame.GetError();
		}

		return HeadBlameRead{.Trackable = true, .Head = payload.substr(0, headSize), .Blame = std::move(blame.GetValue())};
	}

	Core::Result<Core::LineHistory> WslGit::QueryLineHistory(const Core::LineHistoryRequest& request)
	{
		return Decoded(AskForPayload("git.lineHistory", {{"request", Core::ToJson(request)}}), &Core::LineHistoryFromJson);
	}

	Core::Result<WslGit::RangeRead> WslGit::ResolveRange(const std::string& branch, const std::string& base)
	{
		const Core::Result<Answer> answer = Ask("git.resolveRange", {{"branch", branch}, {"base", base}});

		if (!answer)
		{
			return answer.GetError();
		}

		const nlohmann::json& result = answer.GetValue().Result;
		RangeRead range{.Problem = result.value("problem", std::string{"none"}), .Complete = result.value("complete", true)};
		const Core::Result<std::optional<Core::Oid>> mergeBase = OidOf(result, "mergeBase");
		const Core::Result<std::optional<Core::Oid>> tip = OidOf(result, "tip");

		if (!mergeBase || !tip)
		{
			return mergeBase ? tip.GetError() : mergeBase.GetError();
		}

		range.MergeBase = mergeBase.GetValue();
		range.Tip = tip.GetValue();

		if (const auto worktree = result.find("worktree"); worktree != result.end() && worktree->is_string())
		{
			range.Worktree = SharePathOf(worktree->get<std::string>());
		}

		if (range.Problem == "none")
		{
			if (!range.MergeBase || !range.Tip)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's range has no ends"};
			}

			const Core::Result<nlohmann::json> commits = ParsedPayload(answer.GetValue().Payload);
			const Core::Result<Core::LogPage> page =
				commits ? Core::LogPageFromJson({{"commits", commits.GetValue()}, {"complete", range.Complete}, {"walked", 0}, {"unpushed", nlohmann::json::array()}, {"upstreamKnown", false}})
						: Core::Result<Core::LogPage>{commits.GetError()};

			if (!page)
			{
				return page.GetError();
			}

			range.Commits = page.GetValue().Commits;
		}

		return range;
	}

	Core::Result<std::vector<std::optional<std::string>>> WslGit::ReadBlobs(const std::vector<std::pair<Core::Oid, std::filesystem::path>>& reads)
	{
		nlohmann::json listed = nlohmann::json::array();

		for (const auto& [revision, path] : reads)
		{
			listed.push_back({{"revision", revision.ToHex()}, {"path", Core::ToGenericUtf8(path)}});
		}

		const Core::Result<Answer> answer = Ask("git.readBlobs", {{"reads", std::move(listed)}});

		if (!answer)
		{
			return answer.GetError();
		}

		Core::Result<std::vector<std::optional<std::string>>> sides = SplitSides(answer.GetValue().Result, answer.GetValue().Payload);

		if (sides && sides.GetValue().size() != reads.size())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent answered for other blobs than were asked"};
		}

		return sides;
	}

	Core::Result<std::vector<Core::WorktreeEntry>> WslGit::QueryWorktrees()
	{
		Core::Result<std::vector<Core::WorktreeEntry>> worktrees = Decoded(AskForPayload("git.worktrees", nlohmann::json::object()), &Core::WorktreesFromJson);

		if (worktrees)
		{
			for (Core::WorktreeEntry& worktree : worktrees.GetValue())
			{
				worktree.Path = SharePathOf(Core::ToGenericUtf8(worktree.Path));
			}
		}

		return worktrees;
	}

	Core::Result<WorktreeActivity> WslGit::QueryActivity()
	{
		const Core::Result<Answer> answer = Ask("git.activity", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const nlohmann::json& result = answer.GetValue().Result;
		return WorktreeActivity{.Reflog = SecondsFrom(result, "reflog"), .Index = SecondsFrom(result, "index"), .HeadCommit = SecondsFrom(result, "headCommit")};
	}

	Core::Result<WorktreeCount> WslGit::CountUncommitted()
	{
		const Core::Result<Answer> answer = Ask("git.countUncommitted", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		const nlohmann::json& result = answer.GetValue().Result;
		return WorktreeCount{.Changed = result.value("changed", std::size_t{0}), .NewestChange = SecondsFrom(result, "newest")};
	}

	Core::Result<std::pair<std::string, std::string>> WslGit::ReadSinceViewed(const Core::Oid& viewed, const std::optional<Core::Oid>& tip,
																			  const std::optional<std::filesystem::path>& worktree, const std::filesystem::path& path)
	{
		nlohmann::json arguments{{"viewed", viewed.ToHex()}, {"path", Core::ToGenericUtf8(path)}};

		if (tip)
		{
			arguments["tip"] = tip->ToHex();
		}
		else if (worktree)
		{
			const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(*worktree);

			if (!place || !Core::EqualsIgnoringCase(place->Distro, m_Root.Distro))
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the worktree is not in this repository's distro"};
			}

			arguments["worktree"] = place->LinuxPath;
		}

		const Core::Result<Answer> answer = Ask("git.sinceViewed", std::move(arguments));

		if (!answer)
		{
			return answer.GetError();
		}

		Core::Result<std::vector<std::optional<std::string>>> sides = SplitSides(answer.GetValue().Result, answer.GetValue().Payload);

		if (!sides || sides.GetValue().size() != 2 || !sides.GetValue()[0] || !sides.GetValue()[1])
		{
			return sides ? Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's versions are not a viewed and a current one"} : sides.GetError();
		}

		return std::pair{std::move(*sides.GetValue()[0]), std::move(*sides.GetValue()[1])};
	}

	Core::Result<std::vector<Core::GraphCommit>> WslGit::ReadGraphCommits(const Core::GraphReadRequest& request)
	{
		std::vector<Core::Oid> ids = request.Tips;
		ids.insert(ids.end(), request.Known.begin(), request.Known.end());
		const Core::Result<Answer> answer =
			Ask("git.graphCommits", {{"tipCount", request.Tips.size()}}, kLongRequest, kStartWait, Core::EncodeCommitIds(ids));

		if (!answer)
		{
			// Some 4.5 million commits fill the agent's largest answer; a history past that is not read in parts yet.
			if (answer.GetError().Code == Core::ErrorCode::TooLarge)
			{
				return Core::Error{Core::ErrorCode::TooLarge, "this repository's history is too large to read through WSL at once"};
			}

			return answer.GetError();
		}

		Core::Result<std::vector<Core::GraphCommit>> commits = Core::DecodeGraphCommits(answer.GetValue().Payload);

		if (!commits)
		{
			return Core::Error{Core::ErrorCode::IoFailure, std::format("the WSL agent's graph cannot be read: {}", commits.GetError().Message)};
		}

		return commits;
	}

	Core::Result<std::vector<bool>> WslGit::HoldsCommits(const std::vector<Core::Oid>& commits)
	{
		const Core::Result<Answer> answer = Ask("git.holdsCommits", nlohmann::json::object(), kLongRequest, kStartWait, Core::EncodeCommitIds(commits));

		if (!answer)
		{
			return answer.GetError();
		}

		const auto held = answer.GetValue().Result.find("held");

		if (held == answer.GetValue().Result.end() || !held->is_array() || held->size() != commits.size())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent answered for other commits than were asked"};
		}

		std::vector<bool> answers;
		answers.reserve(commits.size());

		for (const nlohmann::json& one : *held)
		{
			if (!one.is_boolean())
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's answer of held commits is not yes or no"};
			}

			answers.push_back(one.get<bool>());
		}

		return answers;
	}

	Core::Result<std::unordered_set<Core::Oid>> WslGit::ShallowBoundary()
	{
		const Core::Result<Answer> answer = Ask("git.shallowBoundary", nlohmann::json::object());

		if (!answer)
		{
			return answer.GetError();
		}

		std::unordered_set<Core::Oid> boundary;
		const auto ids = answer.GetValue().Result.find("ids");

		if (ids == answer.GetValue().Result.end() || !ids->is_array())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent named no shallow boundary"};
		}

		for (const nlohmann::json& text : *ids)
		{
			const std::optional<Core::Oid> id = text.is_string() ? Core::Oid::FromHex(text.get<std::string>()) : std::nullopt;

			if (!id)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's shallow boundary holds something that is not a commit id"};
			}

			boundary.insert(*id);
		}

		return boundary;
	}

	Core::Result<WslGit::IgnoreSetup> WslGit::QueryIgnoreSetup(std::chrono::milliseconds startWait)
	{
		const Core::Result<Answer> answer = Ask("git.ignoreSetup", nlohmann::json::object(), kQuickRequest, startWait);

		if (!answer)
		{
			return answer.GetError();
		}

		const nlohmann::json& result = answer.GetValue().Result;
		const auto workingDirectory = result.find("workingDirectory");
		const auto ruleFiles = result.find("ruleFiles");

		if (workingDirectory == result.end() || !workingDirectory->is_string() || ruleFiles == result.end() || !ruleFiles->is_array())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's ignore setup is incomplete"};
		}

		IgnoreSetup setup;

		// A bare repository has no working directory, as a local one answers.
		if (!workingDirectory->get<std::string>().empty())
		{
			setup.WorkingDirectory = SharePathOf(workingDirectory->get<std::string>());
		}

		for (const nlohmann::json& file : *ruleFiles)
		{
			if (file.is_string())
			{
				setup.RuleFiles.push_back(Core::ToWslSharePath(Core::WslSharePath{m_Root.Distro, file.get<std::string>()}));
			}
		}

		return setup;
	}

	Core::Result<std::vector<bool>> WslGit::QueryIgnored(const std::filesystem::path& folder, const std::vector<Core::DirectoryEntry>& entries,
														 std::chrono::milliseconds startWait)
	{
		const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(folder);

		if (!place || !Core::EqualsIgnoringCase(place->Distro, m_Root.Distro))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "the folder is not in this repository's distro"};
		}

		nlohmann::json listed = nlohmann::json::array();

		for (const Core::DirectoryEntry& entry : entries)
		{
			listed.push_back({{"name", entry.Name}, {"directory", entry.IsDirectory}});
		}

		const Core::Result<Answer> answer = Ask("git.ignored", {{"folder", place->LinuxPath}, {"entries", std::move(listed)}}, kQuickRequest, startWait);

		if (!answer)
		{
			return answer.GetError();
		}

		const auto ignored = answer.GetValue().Result.find("ignored");

		if (ignored == answer.GetValue().Result.end() || !ignored->is_array() || ignored->size() != entries.size())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent answered for other entries than were asked"};
		}

		std::vector<bool> answers;
		answers.reserve(entries.size());

		for (const nlohmann::json& one : *ignored)
		{
			answers.push_back(one.is_boolean() && one.get<bool>());
		}

		return answers;
	}
}

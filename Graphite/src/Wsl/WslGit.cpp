#include "Wsl/WslGit.h"

#include "Wsl/WslAgents.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Remote/GitCodec.h"

#include <format>

namespace Graphite {

	namespace {

		// The agent runs git requests one at a time, so even a small one may wait behind a status of a
		// huge repository: every request gets the long wait, and a write that runs out of it may
		// still land (see Ask).
		constexpr std::chrono::seconds kLongRequest{180};
		constexpr std::chrono::seconds kShortRequest = kLongRequest;

		// How long a request waits for the distro's agent to start.
		constexpr std::chrono::seconds kStartWait{30};

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

	Core::Result<WslGit::Answer> WslGit::Ask(const std::string& operation, nlohmann::json arguments, std::chrono::seconds timeout)
	{
		const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = m_Agents.Get(m_Root.Distro, kStartWait);

		if (!connection)
		{
			return connection.GetError();
		}

		arguments["root"] = m_Root.LinuxPath;
		arguments["reach"] = m_Reach == Reach::Exactly ? "exactly" : "up";

		Core::Result<Core::AgentReply> reply = connection.GetValue()->SendAndWait(operation, std::move(arguments), {}, timeout);

		if (!reply)
		{
			// The agent still has the request: a change may land after Graphite stopped waiting.
			const bool isWrite = operation != "git.status" && operation != "git.untrackedIn" && operation != "git.workingDirectory" &&
								 operation != "git.head" && operation != "git.diff" && operation != "git.readSides" && operation != "git.bases";

			if (reply.GetError().Code == Core::ErrorCode::Cancelled && isWrite)
			{
				return Core::Error{Core::ErrorCode::Cancelled,
								   std::format("the WSL agent did not answer in time; the change may still be made, so refresh before trying again")};
			}

			return reply.GetError();
		}

		return Answer{.Result = std::move(reply.GetValue().Result), .Payload = std::move(reply.GetValue().Payload)};
	}

	Core::Result<void> WslGit::AskForPaths(const std::string& operation, const std::vector<std::filesystem::path>& paths)
	{
		const Core::Result<Answer> answer = Ask(operation, {{"paths", PathsJson(paths)}}, kShortRequest);
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}

	Core::Result<Core::WorkingTreeStatus> WslGit::QueryStatus()
	{
		const Core::Result<Answer> answer = Ask("git.status", nlohmann::json::object(), kLongRequest);

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::WorkingTreeStatusFromJson(parsed.GetValue()) : Core::Result<Core::WorkingTreeStatus>{parsed.GetError()};
	}

	Core::Result<Core::WorkingTreeStatus> WslGit::QueryUntrackedIn(const std::filesystem::path& directory)
	{
		const Core::Result<Answer> answer = Ask("git.untrackedIn", {{"directory", Core::ToGenericUtf8(directory)}}, kLongRequest);

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::WorkingTreeStatusFromJson(parsed.GetValue()) : Core::Result<Core::WorkingTreeStatus>{parsed.GetError()};
	}

	Core::Result<std::filesystem::path> WslGit::GetWorkingDirectory()
	{
		const Core::Result<Answer> answer = Ask("git.workingDirectory", nlohmann::json::object(), kShortRequest);

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
		const Core::Result<Answer> answer = Ask("git.head", nlohmann::json::object(), kShortRequest);

		if (!answer)
		{
			return answer.GetError();
		}

		const Core::Result<nlohmann::json> parsed = ParsedPayload(answer.GetValue().Payload);
		return parsed ? Core::HeadStateFromJson(parsed.GetValue()) : Core::Result<Core::HeadState>{parsed.GetError()};
	}

	Core::Result<Core::Diff> WslGit::QueryDiff(const Core::DiffRequest& request)
	{
		const Core::Result<Answer> answer = Ask("git.diff", {{"request", Core::ToJson(request)}}, kLongRequest);

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

		const Core::Result<Answer> answer = Ask("git.readSides", {{"reads", std::move(listed)}}, kShortRequest);

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

		const Core::Result<Answer> answer = Ask("git.bases", {{"path", place->LinuxPath}}, kShortRequest);

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
			Ask("git.discardAll", {{"untracked", untrackedFiles == Core::UntrackedFiles::Included}}, kLongRequest);
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}

	Core::Result<Core::Oid> WslGit::Commit(const Core::CommitRequest& request)
	{
		const Core::Result<Answer> answer = Ask("git.commit", {{"request", Core::ToJson(request)}}, kShortRequest);

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
		const Core::Result<Answer> answer = Ask("git.init", nlohmann::json::object(), kShortRequest);
		return answer ? Core::Result<void>::Ok() : Core::Result<void>{answer.GetError()};
	}
}

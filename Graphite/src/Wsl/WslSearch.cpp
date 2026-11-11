#include "Wsl/WslSearch.h"

#include "Wsl/WslAgents.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		// How long a search waits for the distro's agent to start.
		constexpr std::chrono::seconds kStartWait{30};

		// A walk of a large folder in the distro; the search lane runs one at a time.
		constexpr std::chrono::seconds kLongestSearch{180};

		// How often a wait looks at its token.
		constexpr std::chrono::milliseconds kCancellationCheck{50};

		// The reply a request waits for; the agent's reader thread fills it, and it outlives a wait
		// that gave up.
		struct PendingReply
		{
			std::mutex Mutex;
			std::condition_variable Arrived;
			std::optional<Core::AgentReply> Reply;
		};

		std::string Joined(const std::string& root, const std::string& relative)
		{
			return root == "/" ? "/" + relative : root + "/" + relative;
		}
	}

	WslSearch::WslSearch(WslAgents& agents, Core::WslSharePath root) : m_Agents(agents), m_Root(std::move(root)) {}

	Core::Result<Core::AgentReply> WslSearch::Ask(std::string operation, nlohmann::json arguments, const Core::CancellationToken& token)
	{
		const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = m_Agents.Get(m_Root.Distro, kStartWait);

		if (!connection)
		{
			return connection.GetError();
		}

		arguments["root"] = m_Root.LinuxPath;
		auto pending = std::make_shared<PendingReply>();

		connection.GetValue()->Send(std::move(operation), std::move(arguments), {}, [pending](const Core::AgentReply& reply) {
			{
				const std::scoped_lock lock{pending->Mutex};
				pending->Reply = reply;
			}
			pending->Arrived.notify_all();
		});

		// Not SendAndWait: a search the panel replaced or closed stops being waited for at once.
		const auto deadline = std::chrono::steady_clock::now() + kLongestSearch;
		std::unique_lock lock{pending->Mutex};

		while (!pending->Reply)
		{
			if (token.IsCancelled())
			{
				return Core::Error{Core::ErrorCode::Cancelled, "the search was cancelled"};
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				return Core::Error{Core::ErrorCode::Cancelled, "the WSL agent did not finish the search in time"};
			}

			pending->Arrived.wait_for(lock, kCancellationCheck);
		}

		Core::AgentReply reply = std::move(*pending->Reply);

		if (reply.Failure)
		{
			return *reply.Failure;
		}

		return reply;
	}

	Core::Result<std::vector<std::string>> WslSearch::ListFiles(bool respectGitIgnore, const Core::CancellationToken& token)
	{
		const Core::Result<Core::AgentReply> reply = Ask("files.list", {{"respectGitIgnore", respectGitIgnore}}, token);

		if (!reply)
		{
			return reply.GetError();
		}

		std::vector<std::string> files;
		const std::string& listed = reply.GetValue().Payload;
		std::size_t start = 0;

		for (std::size_t end = listed.find('\0'); end != std::string::npos; end = listed.find('\0', start))
		{
			files.emplace_back(listed.substr(start, end - start));
			start = end + 1;
		}

		return files;
	}

	Core::Result<FolderSearchFound> WslSearch::SearchText(const SearchQuery& query, bool respectGitIgnore, const std::vector<std::filesystem::path>& skip,
														  const Core::CancellationToken& token)
	{
		nlohmann::json skipped = nlohmann::json::array();
		const std::string inside = Joined(m_Root.LinuxPath, "");

		for (const std::filesystem::path& path : skip)
		{
			const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(path);

			if (place && place->Distro == m_Root.Distro && place->LinuxPath.starts_with(inside))
			{
				skipped.push_back(place->LinuxPath.substr(inside.size()));
			}
		}

		nlohmann::json arguments{{"query", {{"needle", query.Needle}, {"mask", query.Mask}, {"matchCase", query.MatchCase}, {"wholeWord", query.WholeWord}}},
								 {"respectGitIgnore", respectGitIgnore},
								 {"skip", std::move(skipped)}};
		const Core::Result<Core::AgentReply> reply = Ask("search.text", std::move(arguments), token);

		if (!reply)
		{
			return reply.GetError();
		}

		const nlohmann::json hits = nlohmann::json::parse(reply.GetValue().Payload, nullptr, false);

		if (!hits.is_array())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "the WSL agent's search hits are not a list"};
		}

		FolderSearchFound found;
		const nlohmann::json& result = reply.GetValue().Result;
		found.Scanned = result.value("scanned", std::size_t{0});
		found.Skipped = result.value("skipped", std::size_t{0});
		found.Unreadable = result.value("unreadable", std::size_t{0});
		found.Capped = result.value("capped", false);
		found.Hits.reserve(hits.size());

		for (const nlohmann::json& hit : hits)
		{
			if (!hit.is_object() || !hit.contains("file") || !hit.at("file").is_string())
			{
				continue;
			}

			const std::string file = Joined(m_Root.LinuxPath, hit.at("file").get<std::string>());
			found.Hits.push_back(SearchHit{.File = Core::ToWslSharePath(Core::WslSharePath{m_Root.Distro, file}),
										   .Line = hit.value("line", std::size_t{0}),
										   .Text = hit.value("text", std::string{}),
										   .From = hit.value("from", std::size_t{0}),
										   .To = hit.value("to", std::size_t{0})});
		}

		return found;
	}
}

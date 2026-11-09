#include "corepch.h"

#include "Session/AgentSession.h"

#include "Files/FileRequests.h"

#include <unistd.h>

#include <cstdlib>
#include <format>
#include <utility>
#include <variant>

namespace Agent {

	namespace {

		Core::Error MissingArgument(std::string_view key)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("the request has no \"{}\"", key)};
		}
	}

	AgentSession::AgentSession(std::string version) : m_Version(std::move(version)) {}

	void AgentSession::Receive(std::string_view bytes)
	{
		if (m_Ended)
		{
			return;
		}

		m_Reader.Feed(bytes);

		while (!m_Ended)
		{
			const std::optional<Core::Frame> frame = m_Reader.Take();

			if (!frame)
			{
				break;
			}

			Core::Result<Core::AgentMessage> message = Core::DecodeMessage(*frame);
			const Core::AgentRequest* request = message ? std::get_if<Core::AgentRequest>(&message.GetValue()) : nullptr;

			if (request == nullptr)
			{
				// Without a request there is no id to reply to: the stream is not Graphite's.
				m_Ended = true;
				break;
			}

			Handle(*request);
		}

		if (m_Reader.HasFailed())
		{
			m_Ended = true;
		}
	}

	std::string AgentSession::TakeOutput()
	{
		return std::exchange(m_Output, {});
	}

	void AgentSession::Handle(const Core::AgentRequest& request)
	{
		const std::string& operation = request.Operation;

		if (operation == "bye")
		{
			m_Ended = true;
			return;
		}

		if (operation == "hello")
		{
			Reply(request, Hello(request.Arguments));
		}
		else if (operation == "writeFile")
		{
			Reply(request, WriteFile(request.Arguments, request.Payload));
		}
		else if (operation == "rename")
		{
			Reply(request, Rename(request.Arguments));
		}
		else if (operation == "makeDirectory")
		{
			Reply(request, MakeDirectory(request.Arguments));
		}
		else if (operation == "createFile")
		{
			Reply(request, CreateFile(request.Arguments));
		}
		else if (operation == "moveToTrash")
		{
			Reply(request, MoveToTrash(request.Arguments));
		}
		else if (operation == "watch")
		{
			Reply(request, Watch(request.Arguments));
		}
		else if (operation == "unwatch")
		{
			Reply(request, Unwatch(request.Arguments));
		}
		else
		{
			Reply(request, Core::Error{Core::ErrorCode::Unsupported, std::format("the agent has no \"{}\"", operation)});
		}
	}

	void AgentSession::Reply(const Core::AgentRequest& request, Core::Result<nlohmann::json> result)
	{
		Core::AgentReply reply{.Id = request.Id};

		if (result)
		{
			reply.Result = std::move(result.GetValue());
		}
		else
		{
			reply.Failure = result.GetError();
		}

		m_Output += Core::EncodeReply(reply);
	}

	Core::Result<nlohmann::json> AgentSession::Hello(const nlohmann::json& arguments) const
	{
		const std::uint32_t asked = arguments.value("protocol", 0u);

		if (asked != Core::kAgentProtocolVersion)
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("the agent speaks protocol {}, Graphite asked for {}", Core::kAgentProtocolVersion, asked)};
		}

		const char* home = std::getenv("HOME");

		return nlohmann::json{
			{"version", m_Version},
			{"protocol", Core::kAgentProtocolVersion},
			{"home", home != nullptr ? home : ""},
			{"pid", static_cast<long>(::getpid())},
		};
	}

	Core::Result<nlohmann::json> AgentSession::Watch(const nlohmann::json& arguments)
	{
		const auto root = arguments.find("root");

		if (root == arguments.end() || !root->is_string())
		{
			return MissingArgument("root");
		}

		Core::Result<std::unique_ptr<TreeWatch>> watch = TreeWatch::Start(root->get<std::string>());

		if (!watch)
		{
			return watch.GetError();
		}

		const int id = m_NextWatchId++;
		m_Watches.emplace(id, std::move(watch.GetValue()));
		return nlohmann::json{{"watch", id}};
	}

	Core::Result<nlohmann::json> AgentSession::Unwatch(const nlohmann::json& arguments)
	{
		const int id = arguments.value("watch", 0);

		if (m_Watches.erase(id) == 0)
		{
			return Core::Error{Core::ErrorCode::NotFound, std::format("no watch {}", id)};
		}

		m_LimitReported.erase(id);
		return nlohmann::json::object();
	}

	void AgentSession::Tick()
	{
		for (auto& [id, watch] : m_Watches)
		{
			watch->Poll();

			if (watch->HitWatchLimit() && !m_LimitReported[id])
			{
				m_LimitReported[id] = true;
				m_Output += Core::EncodeEvent(Core::AgentEvent{.Name = "watchLimit", .Body = {{"watch", id}}});
			}

			Core::ChangeBatch batch = watch->TakeSettled();

			const bool anything = !batch.Directories.empty() || batch.Overflowed || batch.IgnoreRulesChanged ||
								  batch.RepositoryStateChanged;

			if (!anything)
			{
				continue;
			}

			nlohmann::json directories = nlohmann::json::array();

			for (const std::filesystem::path& directory : batch.Directories)
			{
				directories.push_back(directory.string()); // not a path conversion: Linux paths are their bytes
			}

			m_Output += Core::EncodeEvent(Core::AgentEvent{
				.Name = "changes",
				.Body = {
					{"watch", id},
					{"directories", std::move(directories)},
					{"overflowed", batch.Overflowed},
					{"ignoreRules", batch.IgnoreRulesChanged},
					{"repositoryState", batch.RepositoryStateChanged},
				},
			});
		}
	}

	std::vector<int> AgentSession::WatchDescriptors() const
	{
		std::vector<int> descriptors;
		descriptors.reserve(m_Watches.size());

		for (const auto& [id, watch] : m_Watches)
		{
			descriptors.push_back(watch->GetDescriptor());
		}

		return descriptors;
	}
}

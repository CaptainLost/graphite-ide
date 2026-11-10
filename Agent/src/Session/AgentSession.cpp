#include "corepch.h"

#include "Session/AgentSession.h"

#include "Files/FileRequests.h"

#include "Core/Base/Log.h"
#include "Core/Text/Encoding.h"

#include <unistd.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <set>
#include <utility>
#include <variant>

namespace Agent {

	namespace {

		Core::Error MissingArgument(std::string_view key)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("the request has no \"{}\"", key)};
		}

		// A Linux name is any bytes; the protocol carries UTF-8 (ADR 0695). A folder whose path is
		// not UTF-8 is reported as its nearest parent that is: listing that parent again shows
		// what changed below it, where a mangled name would name nothing.
		std::string NearestUtf8Folder(std::filesystem::path folder)
		{
			while (!Core::IsValidUtf8(folder.native()) && folder.has_parent_path() && folder.parent_path() != folder)
			{
				folder = folder.parent_path();
			}

			return folder.native();
		}
	}

	AgentSession::AgentSession(std::string version, GitHandlers gitHandlers)
		: m_Version(std::move(version)), m_GitHandlers(std::move(gitHandlers))
	{
	}

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
		// One malformed request fails itself, not the agent and every watch it holds.
		try
		{
			HandleOperation(request);
		}
		catch (const std::exception& thrown)
		{
			Reply(request, Core::Error{Core::ErrorCode::InvalidArgument,
									   std::format("cannot handle \"{}\": {}", request.Operation, thrown.what())});
		}
	}

	void AgentSession::HandleOperation(const Core::AgentRequest& request)
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
		else if (operation.starts_with("git."))
		{
			// Answered on the git worker; Tick sends the reply when it is ready.
			const auto handler = m_GitHandlers.find(operation);

			if (handler == m_GitHandlers.end())
			{
				Reply(request, Core::Error{Core::ErrorCode::Unsupported, std::format("the agent has no \"{}\"", operation)});
			}
			else
			{
				m_GitWorker.Submit(request, handler->second);
			}
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
		const auto protocol = arguments.find("protocol");

		if (protocol == arguments.end() || !protocol->is_number_unsigned())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "hello has no protocol number"};
		}

		const std::uint64_t asked = protocol->get<std::uint64_t>();

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
		const auto watch = arguments.find("watch");

		if (watch == arguments.end() || !watch->is_number_integer())
		{
			return MissingArgument("watch");
		}

		const int id = watch->get<int>();

		if (m_Watches.erase(id) == 0)
		{
			return Core::Error{Core::ErrorCode::NotFound, std::format("no watch {}", id)};
		}

		m_LimitReported.erase(id);
		return nlohmann::json::object();
	}

	void AgentSession::Tick()
	{
		for (const Core::AgentReply& reply : m_GitWorker.TakeFinished())
		{
			m_Output += Core::EncodeReply(reply);
		}

		for (auto& [id, watch] : m_Watches)
		{
			// A watch that throws loses this tick's batch, not the agent.
			try
			{
				TickWatch(id, *watch);
			}
			catch (const std::exception& thrown)
			{
				CORE_ERROR("Watch {} failed to report: {}", id, thrown.what());
			}
		}
	}

	void AgentSession::TickWatch(int id, TreeWatch& watch)
	{
		{
			watch.Poll();

			if (watch.HitWatchLimit() && !m_LimitReported[id])
			{
				m_LimitReported[id] = true;
				m_Output += Core::EncodeEvent(Core::AgentEvent{.Name = "watchLimit", .Body = {{"watch", id}}});
			}

			Core::ChangeBatch batch = watch.TakeSettled();

			const bool anything = !batch.Directories.empty() || batch.Overflowed || batch.IgnoreRulesChanged ||
								  batch.RepositoryStateChanged;

			if (!anything)
			{
				return;
			}

			nlohmann::json directories = nlohmann::json::array();
			std::set<std::string> named;

			for (const std::filesystem::path& directory : batch.Directories)
			{
				const std::string reported = NearestUtf8Folder(directory);

				if (named.insert(reported).second)
				{
					directories.push_back(reported);
				}
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
		descriptors.reserve(m_Watches.size() + 1);
		descriptors.push_back(m_GitWorker.GetDescriptor());

		for (const auto& [id, watch] : m_Watches)
		{
			descriptors.push_back(watch->GetDescriptor());
		}

		return descriptors;
	}
}

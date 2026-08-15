#include "Lang/Server/JsonRpc.h"

namespace {

	// Content-Length framing on the way out. MessageStream only reads frames; whatever builds a
	// payload to send is responsible for wrapping it, and that is every method here.
	std::string Frame(const std::string& body)
	{
		return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	}
}

namespace Graphite {

	std::pair<RequestId, std::string> JsonRpc::Request(std::string_view method, const nlohmann::json& params)
	{
		const RequestId id = m_NextId++;
		m_Outstanding.insert(id);

		const nlohmann::json message{
			{"jsonrpc", "2.0"},
			{"id", id},
			{"method", method},
			{"params", params},
		};

		return {id, Frame(message.dump())};
	}

	std::string JsonRpc::Notification(std::string_view method, const nlohmann::json& params)
	{
		const nlohmann::json message{
			{"jsonrpc", "2.0"},
			{"method", method},
			{"params", params},
		};

		return Frame(message.dump());
	}

	std::string JsonRpc::Cancel(RequestId id)
	{
		// A cancelled request's eventual reply, if one arrives at all, must not be delivered to a
		// caller that has moved on — erase the id now rather than waiting for Receive to see it.
		m_Outstanding.erase(id);
		return Notification("$/cancelRequest", {{"id", id}});
	}

	std::optional<IncomingMessage> JsonRpc::Receive(const std::string& body)
	{
		// Non-throwing parse: malformed input yields a discarded value rather than an exception
		// (ADR 0004 — nothing throws across this boundary).
		const nlohmann::json message = nlohmann::json::parse(body, nullptr, false);
		if (message.is_discarded())
		{
			return std::nullopt;
		}

		// Decide on "id" first: a reply carries an id but no method, and a wrongly-ordered check
		// here is how a $/progress notification would get mistaken for an answer, or vice versa.
		if (message.contains("id"))
		{
			const RequestId id = message.at("id").get<RequestId>();
			if (!IsOutstanding(id))
			{
				// Either we never asked, or we did and then cancelled — either way there is no
				// caller left waiting for this reply, so it goes nowhere.
				return std::nullopt;
			}

			m_Outstanding.erase(id);

			IncomingMessage reply;
			reply.ReplyTo = id;
			reply.IsError = message.contains("error");
			reply.Payload = reply.IsError ? message.value("error", nlohmann::json::object())
										   : message.value("result", nlohmann::json::object());
			return reply;
		}

		if (message.contains("method"))
		{
			IncomingMessage notification;
			notification.Method = message.at("method").get<std::string>();
			notification.Payload = message.value("params", nlohmann::json::object());
			return notification;
		}

		return std::nullopt;
	}

	bool JsonRpc::IsOutstanding(RequestId id) const
	{
		return m_Outstanding.contains(id);
	}
}

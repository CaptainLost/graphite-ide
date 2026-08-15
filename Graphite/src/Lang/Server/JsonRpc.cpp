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

		// Decide on "method" first, not "id": a reply carries an id but no method, a notification
		// carries a method but no id, and a server-initiated *request* (clangd sends
		// workspace/configuration and window/workDoneProgress/create early in a session) carries
		// both. Deciding on id first reads that third shape as a reply, finds the id was never
		// one we issued, and drops it — leaving the server waiting forever for an answer it
		// believes we received.
		if (message.contains("method"))
		{
			if (!message.at("method").is_string())
			{
				// A conformant server never sends a non-string method, but nothing here throws.
				return std::nullopt;
			}

			IncomingMessage incoming;
			incoming.Method = message.at("method").get<std::string>();
			incoming.Payload = message.value("params", nlohmann::json::object());

			if (message.contains("id") && message.at("id").is_number_integer())
			{
				// A server-initiated request: remember the id so a later task can answer it.
				incoming.RespondTo = message.at("id").get<RequestId>();
			}

			return incoming;
		}

		if (message.contains("id"))
		{
			if (!message.at("id").is_number_integer())
			{
				// JSON-RPC allows a string id; we only ever issue integers, so a string can never
				// be one of ours. Treat it the same as an id we never asked about, rather than
				// throwing trying to coerce it.
				return std::nullopt;
			}

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

		return std::nullopt;
	}

	bool JsonRpc::IsOutstanding(RequestId id) const
	{
		return m_Outstanding.contains(id);
	}
}

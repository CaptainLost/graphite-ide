#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace Graphite {

	using RequestId = std::int64_t;

	// What Receive hands back once it has decided what a body is: a reply to a request we made,
	// or a method call/notification the server started on its own. Never both — see JsonRpc::Receive.
	struct IncomingMessage
	{
		std::optional<RequestId> ReplyTo;   // set when this is a reply we asked for
		std::string Method;                 // set when this is a server-initiated message
		nlohmann::json Payload;             // "result" for a reply, "params" otherwise
		bool IsError = false;
	};

	// The JSON-RPC half of the language server protocol: numbers requests, frames the bytes to
	// write, and on the way back decides whether a body is a reply we are still waiting on, a
	// server-initiated message, or neither. Framing on the way in is MessageStream's job; framing
	// on the way out belongs here, next to the code that builds the payload being framed.
	class JsonRpc
	{
	public:
		// Returns the framed bytes to write, and records the id as outstanding.
		std::pair<RequestId, std::string> Request(std::string_view method, const nlohmann::json& params);
		std::string Notification(std::string_view method, const nlohmann::json& params);
		std::string Cancel(RequestId id);

		std::optional<IncomingMessage> Receive(const std::string& body);

		bool IsOutstanding(RequestId id) const;

	private:
		RequestId m_NextId = 1;
		std::unordered_set<RequestId> m_Outstanding;
	};
}

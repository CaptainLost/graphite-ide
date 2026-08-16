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

	// What Receive hands back once it has decided what a body is. There are three shapes,
	// distinguished by which of "method" and "id" the body carried:
	//   - method, no id   -> a notification: Method set, ReplyTo/RespondTo both empty.
	//   - no method, id   -> a reply to something we asked: ReplyTo set, Method empty.
	//   - method and id   -> a server-initiated request (e.g. clangd's workspace/configuration):
	//                        Method set, RespondTo set to the id a later reply must carry.
	struct IncomingMessage
	{
		std::optional<RequestId> ReplyTo;   // set when this is a reply we asked for
		std::optional<RequestId> RespondTo; // set when this is a server-initiated request we must answer
		std::string Method;                 // set when this is a server-initiated request or notification
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

		// The answer to a server-initiated request, carrying the id that request arrived under.
		// A server that asks something and is never answered waits forever: clangd asks
		// workspace/configuration and window/workDoneProgress/create before it starts indexing,
		// and holds its progress notifications until both are answered. ErrorReply is how a
		// request nothing here understands is still answered rather than left hanging.
		std::string Reply(RequestId id, const nlohmann::json& result);
		std::string ErrorReply(RequestId id, int code, std::string_view message);

		std::optional<IncomingMessage> Receive(const std::string& body);

		bool IsOutstanding(RequestId id) const;

	private:
		RequestId m_NextId = 1;
		std::unordered_set<RequestId> m_Outstanding;
	};
}

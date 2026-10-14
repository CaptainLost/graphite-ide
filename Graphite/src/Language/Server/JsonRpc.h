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

	// WARNING: RespondTo is the server's own id, kept as the JSON it arrived as. A reply must carry
	// back exactly what was sent, and a request whose id is dropped or rewritten is one the server
	// waits on forever.
	struct IncomingMessage
	{
		std::optional<RequestId> ReplyTo;
		std::optional<nlohmann::json> RespondTo;
		std::string Method;
		nlohmann::json Payload;
		bool IsError = false;
	};

	class JsonRpc
	{
	public:
		std::pair<RequestId, std::string> Request(std::string_view method, const nlohmann::json& params);
		static std::string Notification(std::string_view method, const nlohmann::json& params);
		std::string Cancel(RequestId id);

		std::string Reply(const nlohmann::json& id, const nlohmann::json& result);
		std::string ErrorReply(const nlohmann::json& id, int code, std::string_view message);

		std::optional<IncomingMessage> Receive(const std::string& body);

		void Forget(RequestId id);

		bool IsOutstanding(RequestId id) const;

	private:
		RequestId m_NextId = 1;
		std::unordered_set<RequestId> m_Outstanding;
	};
}

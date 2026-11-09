#pragma once

#include "Core/Base/Result.h"
#include "Core/Remote/Frame.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace Core {

	// What Graphite and its WSL agent say to each other inside frames (ADR 0695). A frame's
	// header is a JSON object whose keys say which kind of message it is.
	inline constexpr std::uint32_t kAgentProtocolVersion = 1;

	// {"id":7,"op":"writeFile","args":{...}}
	struct AgentRequest
	{
		std::uint64_t Id = 0;
		std::string Operation;
		nlohmann::json Arguments = nlohmann::json::object();
		std::string Payload;
	};

	// {"id":7,"ok":true,"result":{...}} or {"id":7,"ok":false,"code":"NotFound","message":"..."}
	struct AgentReply
	{
		std::uint64_t Id = 0;
		std::optional<Error> Failure;
		nlohmann::json Result = nlohmann::json::object();
		std::string Payload;
	};

	// {"event":"changes","body":{...}}, sent by the agent when nothing asked.
	struct AgentEvent
	{
		std::string Name;
		nlohmann::json Body = nlohmann::json::object();
	};

	using AgentMessage = std::variant<AgentRequest, AgentReply, AgentEvent>;

	std::string EncodeRequest(const AgentRequest& request);
	std::string EncodeReply(const AgentReply& reply);
	std::string EncodeEvent(const AgentEvent& event);

	// InvalidArgument when the header is not JSON or not one of the three shapes.
	Result<AgentMessage> DecodeMessage(const Frame& frame);

	std::string_view ErrorCodeName(ErrorCode code);
	std::optional<ErrorCode> ErrorCodeFromName(std::string_view name);
}

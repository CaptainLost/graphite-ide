#include "corepch.h"

#include "Core/Remote/AgentProtocol.h"

#include <array>
#include <utility>

namespace Core {

	namespace {

		struct NamedCode
		{
			ErrorCode Code;
			std::string_view Name;
		};

		constexpr std::array kNamedCodes{
			NamedCode{ErrorCode::None, "None"},
			NamedCode{ErrorCode::NotFound, "NotFound"},
			NamedCode{ErrorCode::AccessDenied, "AccessDenied"},
			NamedCode{ErrorCode::InvalidArgument, "InvalidArgument"},
			NamedCode{ErrorCode::IoFailure, "IoFailure"},
			NamedCode{ErrorCode::Unsupported, "Unsupported"},
			NamedCode{ErrorCode::TooLarge, "TooLarge"},
			NamedCode{ErrorCode::Cancelled, "Cancelled"},
		};

		// A Linux name is any bytes, not always UTF-8, and nlohmann's dump throws on such a string.
		// Replaced rather than thrown: one odd name must not end the conversation. Callers that
		// send paths pick names that are UTF-8 (the agent reports such a folder by its nearest
		// UTF-8 parent), so a replacement here is a last resort, never a path the host acts on.
		std::string DumpHeader(const nlohmann::json& header)
		{
			constexpr int kCompact = -1;
			return header.dump(kCompact, ' ', false, nlohmann::json::error_handler_t::replace);
		}

		Error NotAMessage(std::string_view why)
		{
			return Error{ErrorCode::InvalidArgument, std::string{"not an agent message: "} + std::string{why}};
		}
	}

	std::string_view ErrorCodeName(ErrorCode code)
	{
		for (const NamedCode& named : kNamedCodes)
		{
			if (named.Code == code)
			{
				return named.Name;
			}
		}

		return "IoFailure";
	}

	std::optional<ErrorCode> ErrorCodeFromName(std::string_view name)
	{
		for (const NamedCode& named : kNamedCodes)
		{
			if (named.Name == name)
			{
				return named.Code;
			}
		}

		return std::nullopt;
	}

	std::string EncodeRequest(const AgentRequest& request)
	{
		const nlohmann::json header{{"id", request.Id}, {"op", request.Operation}, {"args", request.Arguments}};
		return EncodeFrame(DumpHeader(header), request.Payload);
	}

	std::string EncodeReply(const AgentReply& reply)
	{
		nlohmann::json header{{"id", reply.Id}, {"ok", !reply.Failure.has_value()}};

		if (reply.Failure)
		{
			header["code"] = ErrorCodeName(reply.Failure->Code);
			header["message"] = reply.Failure->Message;
		}
		else
		{
			header["result"] = reply.Result;
		}

		return EncodeFrame(DumpHeader(header), reply.Payload);
	}

	std::string EncodeEvent(const AgentEvent& event)
	{
		const nlohmann::json header{{"event", event.Name}, {"body", event.Body}};
		return EncodeFrame(DumpHeader(header), "");
	}

	Result<AgentMessage> DecodeMessage(const Frame& frame)
	{
		const nlohmann::json header = nlohmann::json::parse(frame.Header, nullptr, false);

		if (header.is_discarded() || !header.is_object())
		{
			return NotAMessage("the header is not a JSON object");
		}

		try
		{
			if (header.contains("event"))
			{
				return AgentMessage{AgentEvent{
					.Name = header.at("event").get<std::string>(),
					.Body = header.value("body", nlohmann::json::object()),
				}};
			}

			if (header.contains("id") && header.contains("ok"))
			{
				AgentReply reply{.Id = header.at("id").get<std::uint64_t>(), .Payload = frame.Payload};

				if (header.at("ok").get<bool>())
				{
					reply.Result = header.value("result", nlohmann::json::object());
				}
				else
				{
					const std::string codeName = header.value("code", std::string{"IoFailure"});
					reply.Failure = Error{ErrorCodeFromName(codeName).value_or(ErrorCode::IoFailure),
										  header.value("message", std::string{})};
				}

				return AgentMessage{std::move(reply)};
			}

			if (header.contains("id") && header.contains("op"))
			{
				return AgentMessage{AgentRequest{
					.Id = header.at("id").get<std::uint64_t>(),
					.Operation = header.at("op").get<std::string>(),
					.Arguments = header.value("args", nlohmann::json::object()),
					.Payload = frame.Payload,
				}};
			}
		}
		catch (const nlohmann::json::exception& wrongType)
		{
			return NotAMessage(wrongType.what());
		}

		return NotAMessage("neither a request, a reply nor an event");
	}
}

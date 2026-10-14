#include "Language/Server/JsonRpc.h"

#include <cstddef>

namespace {

	constexpr int CompactIndent = -1;
	constexpr char UnusedIndentCharacter = ' ';
	constexpr bool KeepNonAsciiAsUtf8 = false;

	// WARNING: dump() throws on a string that is not valid UTF-8, and such text does reach here: a
	// document whose bytes do not decode is still sent in didOpen. `replace` sends U+FFFD instead.
	std::string Frame(const nlohmann::json& message)
	{
		const std::string body = message.dump(
			CompactIndent, UnusedIndentCharacter, KeepNonAsciiAsUtf8, nlohmann::json::error_handler_t::replace);
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

		return {id, Frame(message)};
	}

	std::string JsonRpc::Notification(std::string_view method, const nlohmann::json& params)
	{
		const nlohmann::json message{
			{"jsonrpc", "2.0"},
			{"method", method},
			{"params", params},
		};

		return Frame(message);
	}

	std::string JsonRpc::Reply(const nlohmann::json& id, const nlohmann::json& result)
	{
		const nlohmann::json message{
			{"jsonrpc", "2.0"},
			{"id", id},
			{"result", result},
		};

		return Frame(message);
	}

	std::string JsonRpc::ErrorReply(const nlohmann::json& id, int code, std::string_view message)
	{
		const nlohmann::json body{
			{"jsonrpc", "2.0"},
			{"id", id},
			{"error", {{"code", code}, {"message", message}}},
		};

		return Frame(body);
	}

	std::string JsonRpc::Cancel(RequestId id)
	{
		m_Outstanding.erase(id);
		return Notification("$/cancelRequest", {{"id", id}});
	}

	void JsonRpc::Forget(RequestId id)
	{
		m_Outstanding.erase(id);
	}

	std::optional<IncomingMessage> JsonRpc::Receive(const std::string& body)
	{
		constexpr std::nullptr_t NoParseCallback = nullptr;
		constexpr bool ThrowOnMalformedInput = false;
		const nlohmann::json message = nlohmann::json::parse(body, NoParseCallback, ThrowOnMalformedInput);

		if (message.is_discarded())
		{
			return std::nullopt;
		}

		// WARNING: "method" is checked before "id". A server-initiated request carries both, and
		// reading it as a reply drops it, leaving the server waiting forever for an answer.
		if (message.contains("method"))
		{
			if (!message.at("method").is_string())
			{
				return std::nullopt;
			}

			IncomingMessage incoming;
			incoming.Method = message.at("method").get<std::string>();
			incoming.Payload = message.value("params", nlohmann::json::object());

			if (message.contains("id"))
			{
				incoming.RespondTo = message.at("id");
			}

			return incoming;
		}

		if (message.contains("id"))
		{
			if (!message.at("id").is_number_integer())
			{
				return std::nullopt;
			}

			const RequestId id = message.at("id").get<RequestId>();

			if (!IsOutstanding(id))
			{
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

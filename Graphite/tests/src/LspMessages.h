#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What LspClient::TakeSent returns is framed: a Content-Length header, a blank line, the body.
inline nlohmann::json BodyOf(const std::string& framed)
{
	const std::size_t start = framed.find("\r\n\r\n");

	if (start == std::string::npos)
	{
		return nlohmann::json{};
	}

	return nlohmann::json::parse(framed.substr(start + 4));
}

// Every sent request or notification for `method`, parsed.
inline std::vector<nlohmann::json> MessagesFor(const std::vector<std::string>& sent, std::string_view method)
{
	std::vector<nlohmann::json> found;

	for (const std::string& framed : sent)
	{
		nlohmann::json body = BodyOf(framed);

		if (body.is_object() && body.value("method", std::string{}) == method)
		{
			found.push_back(std::move(body));
		}
	}

	return found;
}

// The document a textDocument/* message names.
inline std::string UriOf(const nlohmann::json& message)
{
	return message.at("params").at("textDocument").at("uri").get<std::string>();
}

inline std::int64_t IdOf(const nlohmann::json& message)
{
	return message.at("id").get<std::int64_t>();
}

// The initialize answer of a server that answers diagnostic pulls, as Roslyn's carries it.
constexpr std::string_view kHandshakeOfferingPulls =
	R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{"diagnosticProvider":{"interFileDependencies":true,"workspaceDiagnostics":false}}}})";

// A full answer to the pull sent under `id`, carrying `items` (a comma-separated run of diagnostics, or nothing).
inline std::string FullReport(std::int64_t id, std::string_view items)
{
	return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"result":{"kind":"full","resultId":"r","items":[)" +
		   std::string{items} + "]}}";
}

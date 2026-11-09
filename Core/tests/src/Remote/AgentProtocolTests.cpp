#include <doctest/doctest.h>

#include "Core/Remote/AgentProtocol.h"
#include "Core/Remote/Frame.h"

#include <array>
#include <optional>
#include <string>
#include <variant>

namespace {

	Core::Result<Core::AgentMessage> RoundTrip(const std::string& encoded)
	{
		Core::FrameReader reader;
		reader.Feed(encoded);

		const std::optional<Core::Frame> frame = reader.Take();
		REQUIRE(frame.has_value());

		return Core::DecodeMessage(*frame);
	}
}

TEST_CASE("A request round-trips with its operation, arguments and payload")
{
	const Core::AgentRequest request{
		.Id = 7, .Operation = "writeFile", .Arguments = {{"path", "/tmp/a b.txt"}}, .Payload = "contents"};

	const Core::Result<Core::AgentMessage> decoded = RoundTrip(Core::EncodeRequest(request));

	REQUIRE(decoded.HasValue());
	const auto* back = std::get_if<Core::AgentRequest>(&decoded.GetValue());
	REQUIRE(back != nullptr);
	CHECK(back->Id == 7);
	CHECK(back->Operation == "writeFile");
	CHECK(back->Arguments.at("path") == "/tmp/a b.txt");
	CHECK(back->Payload == "contents");
}

TEST_CASE("A successful reply round-trips with its result and payload")
{
	const Core::AgentReply reply{.Id = 3, .Failure = std::nullopt, .Result = {{"watch", 2}}, .Payload = "x"};

	const Core::Result<Core::AgentMessage> decoded = RoundTrip(Core::EncodeReply(reply));

	REQUIRE(decoded.HasValue());
	const auto* back = std::get_if<Core::AgentReply>(&decoded.GetValue());
	REQUIRE(back != nullptr);
	CHECK(back->Id == 3);
	CHECK_FALSE(back->Failure.has_value());
	CHECK(back->Result.at("watch") == 2);
	CHECK(back->Payload == "x");
}

TEST_CASE("A failed reply round-trips with its error code and message")
{
	const Core::AgentReply reply{
		.Id = 4, .Failure = Core::Error{Core::ErrorCode::AccessDenied, "cannot write '/etc/x'"}, .Result = {}, .Payload = {}};

	const Core::Result<Core::AgentMessage> decoded = RoundTrip(Core::EncodeReply(reply));

	REQUIRE(decoded.HasValue());
	const auto* back = std::get_if<Core::AgentReply>(&decoded.GetValue());
	REQUIRE(back != nullptr);
	REQUIRE(back->Failure.has_value());
	CHECK(back->Failure->Code == Core::ErrorCode::AccessDenied);
	CHECK(back->Failure->Message == "cannot write '/etc/x'");
}

TEST_CASE("An event round-trips with its name and body")
{
	const Core::AgentEvent event{.Name = "changes", .Body = {{"watch", 1}, {"overflowed", false}}};

	const Core::Result<Core::AgentMessage> decoded = RoundTrip(Core::EncodeEvent(event));

	REQUIRE(decoded.HasValue());
	const auto* back = std::get_if<Core::AgentEvent>(&decoded.GetValue());
	REQUIRE(back != nullptr);
	CHECK(back->Name == "changes");
	CHECK(back->Body.at("watch") == 1);
}

TEST_CASE("A header that is not a message is an invalid argument")
{
	CHECK(Core::DecodeMessage(Core::Frame{.Header = "not json", .Payload = {}}).GetError().Code ==
		  Core::ErrorCode::InvalidArgument);
	CHECK(Core::DecodeMessage(Core::Frame{.Header = R"({"something":"else"})", .Payload = {}}).GetError().Code ==
		  Core::ErrorCode::InvalidArgument);
	CHECK(Core::DecodeMessage(Core::Frame{.Header = "[1,2]", .Payload = {}}).GetError().Code ==
		  Core::ErrorCode::InvalidArgument);
}

TEST_CASE("Every error code but None has a name that maps back to it")
{
	constexpr std::array codes{Core::ErrorCode::NotFound,	  Core::ErrorCode::AccessDenied, Core::ErrorCode::InvalidArgument,
							   Core::ErrorCode::IoFailure,	  Core::ErrorCode::Unsupported,	 Core::ErrorCode::TooLarge,
							   Core::ErrorCode::Cancelled};

	for (const Core::ErrorCode code : codes)
	{
		CHECK(Core::ErrorCodeFromName(Core::ErrorCodeName(code)) == code);
	}

	CHECK_FALSE(Core::ErrorCodeFromName("NoSuchCode").has_value());
}

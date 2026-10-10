#include <doctest/doctest.h>

#include "Language/Server/Adapters/ServerAdapter.h"

#include <nlohmann/json.hpp>

namespace {

	Graphite::AdapterContext ContextWithSettings(nlohmann::json settings)
	{
		return Graphite::AdapterContext{.Root = {}, .InitializationOptions = nullptr, .Settings = std::move(settings)};
	}
}

TEST_CASE("The plain adapter answers a section by walking the user's settings along its dotted path")
{
	const Graphite::ServerAdapter& plain = Graphite::AdapterOf(Graphite::AdapterKind::Plain);
	const Graphite::AdapterContext context =
		ContextWithSettings(nlohmann::json{{"csharp|inlay_hints", {{"csharp_enable_inlay_hints_for_types", true}}}});

	CHECK(plain.WorkspaceConfiguration(context, "csharp|inlay_hints.csharp_enable_inlay_hints_for_types") == nlohmann::json(true));
}

TEST_CASE("The plain adapter answers a section it cannot find with null")
{
	const Graphite::ServerAdapter& plain = Graphite::AdapterOf(Graphite::AdapterKind::Plain);
	const Graphite::AdapterContext context = ContextWithSettings(nlohmann::json{{"python", {{"analysis", 1}}}});

	CHECK(plain.WorkspaceConfiguration(context, "python.missing").is_null());
	CHECK(plain.WorkspaceConfiguration(context, "python.analysis.deeper").is_null());
}

TEST_CASE("The plain adapter answers an item with no section with every setting")
{
	const Graphite::ServerAdapter& plain = Graphite::AdapterOf(Graphite::AdapterKind::Plain);
	const nlohmann::json settings{{"python", {{"analysis", 1}}}};

	const std::string_view noSection;

	CHECK(plain.WorkspaceConfiguration(ContextWithSettings(settings), noSection) == settings);
}

TEST_CASE("The plain adapter reports no project problem, needs no warm-up open and offers no header switch")
{
	const Graphite::ServerAdapter& plain = Graphite::AdapterOf(Graphite::AdapterKind::Plain);

	CHECK_FALSE(plain.ProjectProblemAt("C:\\").has_value());
	CHECK_FALSE(plain.WarmUpNeedsOpen());
	CHECK_FALSE(plain.SupportsSwitchSourceHeader());
}

TEST_CASE("The plain adapter passes the user's initialization options through and adds nothing")
{
	const Graphite::ServerAdapter& plain = Graphite::AdapterOf(Graphite::AdapterKind::Plain);
	const nlohmann::json options{{"fallbackFlags", nlohmann::json::array({"-std=c++20"})}};
	const Graphite::AdapterContext context{.Root = {}, .InitializationOptions = options, .Settings = nullptr};

	std::vector<Graphite::OutgoingNotification> notifications;
	plain.AfterInitialized(context, notifications);

	CHECK(plain.InitializationOptions(context) == options);
	CHECK(notifications.empty());
	CHECK_FALSE(plain.AnswerRequest(context, "workspace/anything", nlohmann::json::object()).has_value());
}

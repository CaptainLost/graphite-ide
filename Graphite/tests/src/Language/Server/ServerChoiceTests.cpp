#include <doctest/doctest.h>

#include "Language/Server/ServerChoice.h"

#include <optional>
#include <string>

TEST_CASE("A language with nothing stored gets its recommended server")
{
	const Graphite::ServerChoice choice = Graphite::ChooseServer(Graphite::LanguageId::Cpp, std::nullopt);

	CHECK(choice.Kind == Graphite::ServerChoiceKind::Chosen);
	CHECK(choice.Server == Graphite::FindServer("clangd"));
}

TEST_CASE("A language stored as none is turned off")
{
	const std::optional<std::string> stored{std::string{Graphite::kServerTurnedOff}};

	CHECK(Graphite::ChooseServer(Graphite::LanguageId::Cpp, stored).Kind == Graphite::ServerChoiceKind::Off);
}

TEST_CASE("A stored server the language lists is chosen")
{
	const std::optional<std::string> stored{"clangd"};

	CHECK(Graphite::ChooseServer(Graphite::LanguageId::Cpp, stored).Server == Graphite::FindServer("clangd"));
}

TEST_CASE("A stored server the language does not list falls back to the recommended one")
{
	const std::optional<std::string> typo{"clang"};

	const Graphite::ServerChoice choice = Graphite::ChooseServer(Graphite::LanguageId::Cpp, typo);

	CHECK(choice.Kind == Graphite::ServerChoiceKind::Chosen);
	CHECK(choice.Server == Graphite::FindServer("clangd"));
}

TEST_CASE("A language with no servers is not configured whatever is stored")
{
	const std::optional<std::string> stored{"roslyn"};

	CHECK(Graphite::ChooseServer(Graphite::LanguageId::CSharp, stored).Kind == Graphite::ServerChoiceKind::NotConfigured);
	CHECK(Graphite::ChooseServer(Graphite::LanguageId::PlainText, std::nullopt).Kind == Graphite::ServerChoiceKind::NotConfigured);
}

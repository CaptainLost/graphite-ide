#include <doctest/doctest.h>

#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"

#include <optional>
#include <ostream>
#include <string>

TEST_CASE("A language with nothing stored has no server chosen")
{
	CHECK(Graphite::ChooseServer(Graphite::LanguageId::Cpp, std::nullopt).Kind == Graphite::ServerChoiceKind::NotConfigured);
	CHECK(Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::nullopt).Kind == Graphite::ServerChoiceKind::NotConfigured);
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

	CHECK(Graphite::ChooseServer(Graphite::LanguageId::TypeScript, stored).Kind == Graphite::ServerChoiceKind::NotConfigured);
	CHECK(Graphite::ChooseServer(Graphite::LanguageId::PlainText, std::nullopt).Kind == Graphite::ServerChoiceKind::NotConfigured);
}

TEST_CASE("A C# server chosen by id is the one that runs and none turns it off")
{
	const Graphite::ServerChoice chosen = Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{"csharp-ls"});
	CHECK(chosen.Kind == Graphite::ServerChoiceKind::Chosen);
	CHECK(chosen.Server == Graphite::FindServer("csharp-ls"));

	CHECK(Graphite::ChooseServer(Graphite::LanguageId::CSharp, std::string{Graphite::kServerTurnedOff}).Kind ==
		  Graphite::ServerChoiceKind::Off);
}

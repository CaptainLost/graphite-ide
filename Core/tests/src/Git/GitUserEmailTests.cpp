#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <optional>
#include <string>

TEST_CASE("The user's e-mail is read from the repository's configuration without surrounding spaces")
{
	GitFixture fixture;
	fixture.SetIdentity("Ann", "  Ann@Example.com ");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<std::optional<std::string>> email = repository.ReadUserEmail();

	REQUIRE(email.HasValue());
	REQUIRE(email.GetValue().has_value());
	CHECK(*email.GetValue() == "Ann@Example.com");
}

TEST_CASE("A blank user e-mail reads as none")
{
	GitFixture fixture;
	fixture.SetUnusableIdentity();

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<std::optional<std::string>> email = repository.ReadUserEmail();

	REQUIRE(email.HasValue());
	CHECK_FALSE(email.GetValue().has_value());
}

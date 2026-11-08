#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"
#include "TempDirectory.h"

#include <git2.h>

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

TEST_CASE("core.sshCommand counts as configured when set, also when empty, and not when unset")
{
	GitFixture fixture;
	git_libgit2_init();
	git_repository* handle = nullptr;
	REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);
	git_config* config = nullptr;
	REQUIRE(git_repository_config(&config, handle) == 0);

	// WARNING: The search paths above the repository are pointed at nothing so that a machine-wide core.sshCommand
	// cannot reach the unset case.
	TempDirectory empty;
	const std::string emptyPath = Core::ToUtf8(empty.GetPath());

	for (const git_config_level_t level : {GIT_CONFIG_LEVEL_SYSTEM, GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL,
										 GIT_CONFIG_LEVEL_PROGRAMDATA})
	{
		git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, static_cast<int>(level), emptyPath.c_str());
	}

	const Core::Repository unset = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<bool> unsetAnswer = unset.IsSshCommandConfigured();
	REQUIRE(unsetAnswer.HasValue());
	CHECK_FALSE(unsetAnswer.GetValue());

	REQUIRE(git_config_set_string(config, "core.sshCommand", "") == 0);
	const Core::Repository blank = OpenRepositoryAt(fixture.GetPath());
	CHECK(blank.IsSshCommandConfigured().GetValue());

	REQUIRE(git_config_set_string(config, "core.sshCommand", "plink -batch") == 0);
	const Core::Repository set = OpenRepositoryAt(fixture.GetPath());
	CHECK(set.IsSshCommandConfigured().GetValue());

	for (const git_config_level_t level : {GIT_CONFIG_LEVEL_SYSTEM, GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL,
										 GIT_CONFIG_LEVEL_PROGRAMDATA})
	{
		git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, static_cast<int>(level), nullptr);
	}

	git_config_free(config);
	git_repository_free(handle);
}

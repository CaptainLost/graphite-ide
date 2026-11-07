#include <doctest/doctest.h>

#include "Git/Window/GitWindowRestore.h"

#include <optional>
#include <string>
#include <vector>

namespace {

	using namespace Graphite;

	Core::Branch Local(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		return branch;
	}

	Core::Branch Remote(const std::string& name)
	{
		Core::Branch branch;
		branch.Kind = Core::RefKind::Remote;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		return branch;
	}

	Core::Tag TagNamed(const std::string& name)
	{
		Core::Tag tag;
		tag.Name = name;
		tag.FullName = "refs/tags/" + name;
		return tag;
	}

	const std::vector<Core::Branch> kBranches = {Local("main"), Local("topic"), Remote("origin/side")};
	const std::vector<Core::Tag> kTags = {TagNamed("v2"), TagNamed("release/1.0"), TagNamed("topic")};

	const RefListing kListed{.Listed = true};
	const RefListing kUnread{};
	const RefListing kFailed{.Failed = true};
}

TEST_CASE("Git window: a restored ref that is gone shows head's history")
{
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/remotes/origin/gone"}, kBranches, kTags).has_value());
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/heads/topic"}, {}, kTags).has_value());
	CHECK_FALSE(RefToRestore(GitWindowState{}, kBranches, kTags).has_value());

	CHECK(RefToRestore(GitWindowState{.Ref = "refs/remotes/origin/side"}, kBranches, kTags) ==
		  std::optional<std::string>{"refs/remotes/origin/side"});
	CHECK(RefToRestore(GitWindowState{.Ref = "refs/heads/topic"}, kBranches, kTags) ==
		  std::optional<std::string>{"refs/heads/topic"});
}

TEST_CASE("Git window: a restored ref waits for the branches and nothing else does")
{
	const GitWindowState named{.Ref = "refs/heads/topic"};

	CHECK(RestoreWaitsForListing(named, kUnread, kListed));
	CHECK_FALSE(RestoreWaitsForListing(named, kListed, kUnread));
	CHECK_FALSE(RestoreWaitsForListing(named, kFailed, kUnread));
	CHECK_FALSE(RestoreWaitsForListing(GitWindowState{}, kUnread, kUnread));
}

TEST_CASE("Git window restore: a listed tag's full name restores its history")
{
	CHECK(RefToRestore(GitWindowState{.Ref = "refs/tags/release/1.0"}, kBranches, kTags) ==
		  std::optional<std::string>{"refs/tags/release/1.0"});

	// A tag and a branch of the same short name are two refs: each restores its own.
	CHECK(RefToRestore(GitWindowState{.Ref = "refs/tags/topic"}, kBranches, kTags) == std::optional<std::string>{"refs/tags/topic"});
	CHECK(RefToRestore(GitWindowState{.Ref = "refs/tags/topic"}, {}, kTags) == std::optional<std::string>{"refs/tags/topic"});
}

TEST_CASE("Git window restore: a tag no longer listed falls back to HEAD")
{
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/tags/v1"}, kBranches, kTags).has_value());
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/tags/v2"}, kBranches, {}).has_value());

	// A branch of its short name does not stand for it, nor a tag for a branch.
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/tags/main"}, kBranches, kTags).has_value());
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/heads/v2"}, kBranches, kTags).has_value());
}

TEST_CASE("Git window restore: restoring a tag waits for the listing")
{
	const GitWindowState tag{.Ref = "refs/tags/v2"};

	CHECK(RestoreWaitsForListing(tag, kListed, kUnread));
	CHECK(RestoreWaitsForListing(tag, kUnread, kUnread));
	CHECK_FALSE(RestoreWaitsForListing(tag, kUnread, kListed));

	// A failed tag listing ends the wait, on HEAD's history.
	CHECK_FALSE(RestoreWaitsForListing(tag, kListed, kFailed));
	CHECK_FALSE(RefToRestore(tag, kBranches, {}).has_value());
}

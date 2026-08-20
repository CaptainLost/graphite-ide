#include <doctest/doctest.h>

#include "Git/CommitLabels.h"

using namespace std::chrono_literals;

namespace {

	std::chrono::sys_seconds At(long long seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}
}

TEST_CASE("An age under a minute reads as just now")
{
	CHECK(Graphite::RelativeAge(At(1000), At(1030)) == "just now");
}

TEST_CASE("Ages read in the largest unit that fits")
{
	CHECK(Graphite::RelativeAge(At(0), At(120)) == "2 minutes ago");
	CHECK(Graphite::RelativeAge(At(0), At(7200)) == "2 hours ago");
	CHECK(Graphite::RelativeAge(At(0), At(172800)) == "2 days ago");
}

TEST_CASE("A single unit is not pluralised")
{
	CHECK(Graphite::RelativeAge(At(0), At(60)) == "1 minute ago");
	CHECK(Graphite::RelativeAge(At(0), At(3600)) == "1 hour ago");
}

TEST_CASE("A commit dated in the future reads as just now rather than negatively")
{
	// Clock skew between machines is ordinary, and "in -3 minutes" is not a thing to show
	// a user.
	CHECK(Graphite::RelativeAge(At(5000), At(1000)) == "just now");
}

TEST_CASE("Initials come from the first and last word of a name")
{
	Core::Signature who;
	who.Name = "Ada Lovelace";

	CHECK(Graphite::AuthorInitials(who) == "AL");
}

TEST_CASE("A one-word name gives one initial")
{
	Core::Signature who;
	who.Name = "Ada";

	CHECK(Graphite::AuthorInitials(who) == "A");
}

TEST_CASE("A name with a middle name still gives two initials")
{
	Core::Signature who;
	who.Name = "Ada King Lovelace";

	CHECK(Graphite::AuthorInitials(who) == "AL");
}

TEST_CASE("An empty name falls back to the email")
{
	Core::Signature who;
	who.Email = "ada@example.com";

	// A commit with no author name is malformed but real, and a blank badge tells the
	// reader nothing at all.
	CHECK(Graphite::AuthorInitials(who) == "A");
}

TEST_CASE("A name with neither name nor email gives a placeholder")
{
	CHECK(Graphite::AuthorInitials(Core::Signature{}) == "?");
}

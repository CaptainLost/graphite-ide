#include <doctest/doctest.h>

#include "Git/Tags/TagLookup.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	Core::Tag TagNamed(const std::string& name)
	{
		Core::Tag tag;
		tag.Name = name;
		tag.FullName = "refs/tags/" + name;
		return tag;
	}
}

TEST_CASE("Tag lookup: a tag is found by its full name, at its place in the list")
{
	const std::vector<Core::Tag> tags = {TagNamed("v2"), TagNamed("release/1.0"), TagNamed("v1")};

	CHECK(IndexOfTag(tags, "refs/tags/release/1.0") == std::optional<std::size_t>{1});
	CHECK(TagWithFullName(tags, "refs/tags/release/1.0") == &tags[1]);

	// Not by its short name, nor by a branch's full name that shares it.
	CHECK_FALSE(IndexOfTag(tags, "v1").has_value());
	CHECK_FALSE(IndexOfTag(tags, "refs/heads/v1").has_value());
	CHECK(TagWithFullName(tags, "refs/heads/v1") == nullptr);
	CHECK_FALSE(IndexOfTag({}, "refs/tags/v1").has_value());
}

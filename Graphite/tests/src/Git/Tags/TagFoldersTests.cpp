#include <doctest/doctest.h>

#include "Git/Tags/TagFolders.h"

#include <cstddef>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	std::vector<Core::Tag> TagsNamed(const std::vector<std::string>& names)
	{
		std::vector<Core::Tag> tags;

		for (const std::string& name : names)
		{
			Core::Tag tag;
			tag.Name = name;
			tag.FullName = "refs/tags/" + name;
			tags.push_back(std::move(tag));
		}

		return tags;
	}

	std::vector<std::size_t> AllOf(const std::vector<Core::Tag>& tags)
	{
		std::vector<std::size_t> all;

		for (std::size_t index = 0; index < tags.size(); ++index)
		{
			all.push_back(index);
		}

		return all;
	}
}

TEST_CASE("Tag folders: a name with a slash goes into the folder of its first segment, placed where its newest tag is")
{
	const std::vector<Core::Tag> tags = TagsNamed({"v3", "rel/2", "v2", "rel/1", "v1"});
	const std::vector<std::size_t> shown = AllOf(tags);

	const std::vector<TagFolderItem> items = FoldTags(tags, shown);

	REQUIRE(items.size() == 4);
	CHECK(items[0].Tag == 0);
	CHECK(items[1].Tag == std::nullopt);
	CHECK(items[1].Folder == "rel");
	CHECK(items[1].Tags == std::vector<std::size_t>{1, 3});
	CHECK(items[2].Tag == 2);
	CHECK(items[3].Tag == 4);
}

TEST_CASE("Tag folders: a deeper name keeps the rest of its path")
{
	const std::vector<Core::Tag> tags = TagsNamed({"a/b/c", "a/d"});
	const std::vector<TagFolderItem> items = FoldTags(tags, AllOf(tags));

	REQUIRE(items.size() == 1);
	CHECK(items[0].Folder == "a");
	CHECK(items[0].Tags == std::vector<std::size_t>{0, 1});
	CHECK(FolderOfTagName(tags[0].Name) == "a");
	CHECK(tags[0].Name.substr(FolderOfTagName(tags[0].Name).size() + 1) == "b/c");
}

TEST_CASE("Tag folders: only the shown tags are folded and none shown gives nothing")
{
	const std::vector<Core::Tag> tags = TagsNamed({"rel/2", "v2", "rel/1"});
	const std::vector<std::size_t> shown = {1, 2};

	const std::vector<TagFolderItem> items = FoldTags(tags, shown);

	REQUIRE(items.size() == 2);
	CHECK(items[0].Tag == 1);
	CHECK(items[1].Folder == "rel");
	CHECK(items[1].Tags == std::vector<std::size_t>{2});
	CHECK(FoldTags(tags, {}).empty());
}

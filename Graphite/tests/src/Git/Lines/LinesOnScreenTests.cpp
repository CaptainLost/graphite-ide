#include <doctest/doctest.h>

#include "Git/Lines/LinesOnScreen.h"

#include "Git/Lines/LineReader.h"

#include "Core/Base/Base.h"
#include "Core/Git/Ref.h"

#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	Core::Ref<const LineRead> ReadWithHeadOn(std::size_t head)
	{
		std::vector<Core::Branch> branches(2);
		branches[0].FullName = "refs/heads/main";
		branches[1].FullName = "refs/heads/feature";
		branches[head].IsHead = true;

		LineRead read;
		read.Refs.resize(2);
		read.Refs[0].FullName = "refs/heads/main";
		read.Refs[1].FullName = "refs/heads/feature";
		read.Branches = Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches));
		return Core::CreateRef<const LineRead>(std::move(read));
	}
}

TEST_CASE("Git window: the lines on screen follow a new read and keep the one held")
{
	LinesOnScreen onScreen;

	CHECK(onScreen.GetLines() == nullptr);
	CHECK(onScreen.CheckedOutName().empty());

	const Core::Ref<const LineRead> first = ReadWithHeadOn(0);
	onScreen.Follow(first);

	REQUIRE(onScreen.GetLines() != nullptr);
	CHECK(onScreen.GetRead() == first);
	CHECK(onScreen.GetHeadRef() == std::optional<std::size_t>{0});
	CHECK(onScreen.CheckedOutName() == "refs/heads/main");

	const BranchLines* const lines = onScreen.GetLines();
	onScreen.Follow(first);
	CHECK(onScreen.GetLines() == lines);

	onScreen.Follow(ReadWithHeadOn(1));
	CHECK(onScreen.CheckedOutName() == "refs/heads/feature");
	CHECK(onScreen.GetLines()->IndexOf("refs/heads/feature") == std::optional<std::size_t>{1});

	onScreen.Follow(nullptr);
	CHECK(onScreen.GetRead() == nullptr);
	CHECK(onScreen.GetLines() == nullptr);
	CHECK_FALSE(onScreen.GetHeadRef().has_value());
}

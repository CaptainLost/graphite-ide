#include <doctest/doctest.h>

#include "Workspace/OwnStateChanges.h"

using namespace Graphite;

namespace {

	const std::filesystem::path kRoot = std::filesystem::path("C:/Work/Project");
	const std::vector<std::filesystem::path> kEmptyBatch;
}

TEST_CASE("A batch that only touched the IDE's own state directory is its own writing")
{
	CHECK(OnlyOwnStateChanged({kRoot / ".graphite"}, kRoot));
}

TEST_CASE("A batch under the state directory's subdirectories is still its own writing")
{
	CHECK(OnlyOwnStateChanged({kRoot / ".graphite", kRoot / ".graphite" / "analysis"}, kRoot));
}

TEST_CASE("A batch that also touched a source directory is not only the IDE's own writing")
{
	CHECK_FALSE(OnlyOwnStateChanged({kRoot / ".graphite", kRoot / "src"}, kRoot));
}

TEST_CASE("The folder root itself is not inside the state directory")
{
	CHECK_FALSE(OnlyOwnStateChanged({kRoot}, kRoot));
}

TEST_CASE("A directory whose name only begins like the state directory is not inside it")
{
	CHECK_FALSE(OnlyOwnStateChanged({kRoot / ".graphite-old"}, kRoot));
}

TEST_CASE("An empty batch is not the IDE's own writing")
{
	CHECK_FALSE(OnlyOwnStateChanged(kEmptyBatch, kRoot));
}

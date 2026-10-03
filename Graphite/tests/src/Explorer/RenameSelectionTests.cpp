#include <doctest/doctest.h>

#include "Explorer/RenameSelection.h"

using Graphite::RenameSelection;
using Graphite::RenameSpan;

TEST_CASE("RenameSelection selects the stem of a file")
{
	CHECK(RenameSelection("a.txt", false) == RenameSpan{0, 1});
}

TEST_CASE("RenameSelection selects up to the last dot of a compound extension")
{
	CHECK(RenameSelection("archive.tar.gz", false) == RenameSpan{0, 11});
}

TEST_CASE("RenameSelection selects a dotfile whole")
{
	CHECK(RenameSelection(".gitignore", false) == RenameSpan{0, 10});
}

TEST_CASE("RenameSelection selects a file without an extension whole")
{
	CHECK(RenameSelection("Makefile", false) == RenameSpan{0, 8});
}

TEST_CASE("RenameSelection selects a folder whole even when its name has a dot")
{
	CHECK(RenameSelection("a.b", true) == RenameSpan{0, 3});
}

TEST_CASE("RenameSelection of an empty name is empty")
{
	CHECK(RenameSelection("", false) == RenameSpan{0, 0});
}

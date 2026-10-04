#include <doctest/doctest.h>

#include "Explorer/PendingFolderRename.h"

using Graphite::PendingFolderRename;

TEST_CASE("A pending folder rename waits, without looking, until the solution loads again")
{
	PendingFolderRename rename;
	rename.Request("Tools/New Folder", 4);

	for (int frame = 0; frame < 3; ++frame)
	{
		rename.BeginFrame(true, 4);

		CHECK_FALSE(rename.IsLooking());
		CHECK_FALSE(rename.LeadsTo("Tools"));
		CHECK_FALSE(rename.Take("Tools/New Folder"));

		rename.EndFrame();
		CHECK(rename.IsPending());
	}
}

TEST_CASE("A pending folder rename starts on the load that lists the folder")
{
	PendingFolderRename rename;
	rename.Request("Tools/New Folder", 4);
	rename.BeginFrame(true, 5);

	CHECK(rename.IsLooking());
	CHECK(rename.LeadsTo("Tools"));
	CHECK_FALSE(rename.LeadsTo("Tool"));
	CHECK_FALSE(rename.Take("Tools"));
	CHECK(rename.Take("Tools/New Folder"));
	CHECK_FALSE(rename.IsPending());

	rename.EndFrame();
	CHECK_FALSE(rename.IsPending());
}

TEST_CASE("A pending folder rename is dropped by a load whose walk did not reach the folder")
{
	PendingFolderRename rename;
	rename.Request("New Folder", 4);
	rename.BeginFrame(true, 5);
	rename.EndFrame();

	CHECK_FALSE(rename.IsPending());

	// A later load that does list the folder no longer renames it.
	rename.BeginFrame(true, 6);
	CHECK_FALSE(rename.IsLooking());
	CHECK_FALSE(rename.Take("New Folder"));
}

TEST_CASE("A pending folder rename is dropped when the solution is gone or cannot be edited")
{
	PendingFolderRename rename;
	rename.Request("New Folder", 4);
	rename.BeginFrame(false, 4);

	CHECK_FALSE(rename.IsPending());
	CHECK_FALSE(rename.IsLooking());
}

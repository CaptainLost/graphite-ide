#include <doctest/doctest.h>

#include "Solution/ProjectContentReader.h"
#include "Solution/SolutionModel.h"

using namespace Graphite;

TEST_CASE("SortContentByName orders folders and files by name, ignoring case, at every depth")
{
	ContentFolder zeta{.Name = "Zeta"};
	zeta.Files = {ContentFile{.Name = "z.cs"}, ContentFile{.Name = "m.cs"}};

	ContentFolder root;
	root.Folders = {zeta, ContentFolder{.Name = "alpha"}};
	root.Files = {ContentFile{.Name = "b.cs"}, ContentFile{.Name = "A.cs"}};

	SortContentByName(root);

	CHECK(root.Folders[0].Name == "alpha");
	CHECK(root.Folders[1].Name == "Zeta");
	CHECK(root.Files[0].Name == "A.cs");
	CHECK(root.Folders[1].Files[0].Name == "m.cs");
}

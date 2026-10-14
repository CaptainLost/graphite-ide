#include <doctest/doctest.h>

#include "UI/ModalQuestion.h"

#include <imgui_internal.h>

#include <string>

namespace {

	bool IdentifiedAsStable(const std::string& name, const char* stableId)
	{
		return ImHashStr(name.c_str()) == ImHashStr(stableId);
	}

	std::string Shown(const std::string& name)
	{
		return name.substr(0, name.find("##"));
	}
}

TEST_CASE("A modal's window name shows the visible title and is identified by the stable id")
{
	const std::string name = Graphite::ModalWindowName("Delete branch feature/x", "###GitDeleteBranch");

	CHECK(Shown(name) == "Delete branch feature/x");
	CHECK(IdentifiedAsStable(name, "###GitDeleteBranch"));
}

TEST_CASE("A visible title ending in # does not run into the stable id")
{
	const std::string name = Graphite::ModalWindowName("Delete branch fix#", "###GitDeleteBranch");

	CHECK(IdentifiedAsStable(name, "###GitDeleteBranch"));
	CHECK(Shown(name).starts_with("Delete branch fix#"));
}

TEST_CASE("Hashes inside a visible title cannot become the window's identity")
{
	for (const char* visible : {"Delete branch a###b", "Rename a##b", "Rename ##", "Rename a####"})
	{
		CAPTURE(visible);
		const std::string name = Graphite::ModalWindowName(visible, "###GitRenameBranch");

		CHECK(IdentifiedAsStable(name, "###GitRenameBranch"));
		CHECK(name.ends_with("###GitRenameBranch"));
	}
}

TEST_CASE("An empty visible title leaves only the stable id")
{
	CHECK(Graphite::ModalWindowName("", "###GitCheckout") == "###GitCheckout");
}

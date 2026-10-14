#include <doctest/doctest.h>

#include <pugixml.hpp>

#include <string>

TEST_CASE("pugixml is compiled in and parses a .slnx-shaped document and reads attributes")
{
	constexpr const char* kDocument =
		R"(<Solution><Folder Name="/App/"><Project Path="IDE\IDE.vcxproj" /></Folder></Solution>)";

	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_string(kDocument);

	REQUIRE(result);

	const pugi::xml_node folder = document.child("Solution").child("Folder");
	CHECK(std::string{folder.attribute("Name").value()} == "/App/");
	CHECK(std::string{folder.child("Project").attribute("Path").value()} == "IDE\\IDE.vcxproj");
}

TEST_CASE("pugixml reports a parse failure instead of throwing, which the Result-returning readers rely on")
{
	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_string("<Solution><unclosed>");

	CHECK_FALSE(result);
	CHECK(std::string{result.description()}.length() > 0);
}

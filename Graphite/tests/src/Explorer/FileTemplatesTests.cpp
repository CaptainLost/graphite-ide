#include <doctest/doctest.h>

#include "Explorer/FileTemplates.h"

using Graphite::CSharpNamespaceFor;
using Graphite::ExpandTemplate;
using Graphite::FileTemplate;
using Graphite::TemplateNameProblem;

TEST_CASE("An empty file template makes one file with the typed name and nothing in it")
{
	const auto files = ExpandTemplate(FileTemplate::Empty, "notes.md", "");

	REQUIRE(files.size() == 1);
	CHECK(files[0].Name == "notes.md");
	CHECK(files[0].Content.empty());
}

TEST_CASE("A C++ class makes a header with the class in it and a source that includes it")
{
	const auto files = ExpandTemplate(FileTemplate::CppClass, "Foo", "");

	REQUIRE(files.size() == 2);
	CHECK(files[0].Name == "Foo.h");
	CHECK(files[0].Content.find("#pragma once") != std::string::npos);
	CHECK(files[0].Content.find("class Foo") != std::string::npos);
	CHECK(files[1].Name == "Foo.cpp");
	CHECK(files[1].Content == "#include \"Foo.h\"\n");
}

TEST_CASE("A C++ class is indented with the indentation it is given")
{
	const auto spaces = ExpandTemplate(FileTemplate::CppClass, "Foo", "", "    ");
	CHECK(spaces[0].Content.find("\n    Foo() = default;") != std::string::npos);
	CHECK(spaces[0].Content.find('\t') == std::string::npos);

	const auto tabs = ExpandTemplate(FileTemplate::CppClass, "Foo", "");
	CHECK(tabs[0].Content.find("\n\tFoo() = default;") != std::string::npos);
}

TEST_CASE("A C# class sits in the namespace it is given")
{
	const auto files = ExpandTemplate(FileTemplate::CSharpClass, "Foo", "App.Models");

	REQUIRE(files.size() == 1);
	CHECK(files[0].Name == "Foo.cs");
	CHECK(files[0].Content.find("namespace App.Models;") != std::string::npos);
	CHECK(files[0].Content.find("public class Foo") != std::string::npos);
}

TEST_CASE("A class name must be an identifier, a file name may be anything valid")
{
	CHECK_FALSE(TemplateNameProblem(FileTemplate::CppClass, "Foo_2").has_value());
	CHECK(TemplateNameProblem(FileTemplate::CppClass, "2Foo").has_value());
	CHECK(TemplateNameProblem(FileTemplate::CSharpClass, "Foo.Bar").has_value());
	CHECK(TemplateNameProblem(FileTemplate::CppClass, "util/Foo").has_value());
	CHECK_FALSE(TemplateNameProblem(FileTemplate::Empty, "util/2 Foo.txt").has_value());
	CHECK_FALSE(TemplateNameProblem(FileTemplate::CppClass, "").has_value());
}

TEST_CASE("The C# namespace is the folders below the root, each made into an identifier")
{
	CHECK(CSharpNamespaceFor({"Models", "Users"}, "App") == "Models.Users");
	CHECK(CSharpNamespaceFor({"my-lib", "2d"}, "App") == "my_lib._2d");
}

TEST_CASE("A C# file at the root takes the root folder's name as its namespace")
{
	CHECK(CSharpNamespaceFor({}, "My App") == "My_App");
	CHECK(CSharpNamespaceFor({}, "") == "Project");
}

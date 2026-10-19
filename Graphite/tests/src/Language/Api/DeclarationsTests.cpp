#include <doctest/doctest.h>

#include "Language/Api/Declarations.h"

#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {

	constexpr int kFirstDeclaration = 0;
	constexpr int kSecondDeclaration = 1;

	Declaration Spanning(std::string name, DeclarationKind kind, std::size_t line, std::size_t endLine,
						 int parent = kNoParentDeclaration)
	{
		Declaration declaration;
		declaration.Name = std::move(name);
		declaration.Kind = kind;
		declaration.Line = line;
		declaration.NameLine = line;
		declaration.EndLine = endLine;
		declaration.Parent = parent;

		return declaration;
	}

	std::vector<Declaration> RendererWithTwoMethods()
	{
		return {Spanning("Renderer", DeclarationKind::Type, 2, 20),
				Spanning("Draw", DeclarationKind::Function, 4, 9, kFirstDeclaration),
				Spanning("Clear", DeclarationKind::Function, 12, 15, kFirstDeclaration)};
	}

	std::vector<std::string> NamesOf(const std::vector<const Declaration*>& chain)
	{
		std::vector<std::string> names;

		for (const Declaration* declaration : chain)
		{
			names.push_back(declaration->Name);
		}

		return names;
	}
}

TEST_CASE("The declarations containing a line inside a method are its class and then the method")
{
	const std::vector<Declaration> declarations = RendererWithTwoMethods();

	CHECK(NamesOf(InnermostDeclarationAt(declarations, 6)) == std::vector<std::string>{"Renderer", "Draw"});
	CHECK(NamesOf(InnermostDeclarationAt(declarations, 13)) == std::vector<std::string>{"Renderer", "Clear"});
}

TEST_CASE("A line between a class's methods is inside the class alone")
{
	CHECK(NamesOf(InnermostDeclarationAt(RendererWithTwoMethods(), 10)) == std::vector<std::string>{"Renderer"});
}

TEST_CASE("A declaration's first and last lines are inside it")
{
	const std::vector<Declaration> declarations = RendererWithTwoMethods();

	CHECK(NamesOf(InnermostDeclarationAt(declarations, 4)) == std::vector<std::string>{"Renderer", "Draw"});
	CHECK(NamesOf(InnermostDeclarationAt(declarations, 9)) == std::vector<std::string>{"Renderer", "Draw"});
	CHECK(NamesOf(InnermostDeclarationAt(declarations, 20)) == std::vector<std::string>{"Renderer"});
}

TEST_CASE("A line no declaration contains has no chain")
{
	const std::vector<Declaration> declarations = RendererWithTwoMethods();

	CHECK(InnermostDeclarationAt(declarations, 0).empty());
	CHECK(InnermostDeclarationAt(declarations, 21).empty());
	CHECK(InnermostDeclarationAt({}, 0).empty());
}

TEST_CASE("A method on its class's own line is the innermost of the two")
{
	const std::vector<Declaration> declarations = {Spanning("Point", DeclarationKind::Type, 3, 3),
												   Spanning("Length", DeclarationKind::Function, 3, 3, kFirstDeclaration)};

	CHECK(NamesOf(InnermostDeclarationAt(declarations, 3)) == std::vector<std::string>{"Point", "Length"});
}

TEST_CASE("Declarations that name no parent give only the innermost one, however their ranges nest")
{
	const std::vector<Declaration> declarations = {Spanning("Renderer", DeclarationKind::Type, 2, 20),
												   Spanning("Draw", DeclarationKind::Function, 4, 9)};

	CHECK(NamesOf(InnermostDeclarationAt(declarations, 6)) == std::vector<std::string>{"Draw"});
	CHECK(NamesOf(InnermostDeclarationAt(declarations, 10)) == std::vector<std::string>{"Renderer"});
}

TEST_CASE("Lines inserted above a declaration move its start, its name and its end")
{
	std::vector<Declaration> declarations = {Spanning("Draw", DeclarationKind::Function, 4, 9)};
	declarations[0].NameLine = 5;

	MoveDeclarations(declarations, 1, 3);

	REQUIRE(declarations.size() == 1);
	CHECK(declarations[0].Line == 7);
	CHECK(declarations[0].NameLine == 8);
	CHECK(declarations[0].EndLine == 12);
}

TEST_CASE("A line inserted inside a declaration extends its end and leaves its start")
{
	std::vector<Declaration> declarations = RendererWithTwoMethods();

	MoveDeclarations(declarations, 6, 1);

	CHECK(declarations[0].Line == 2);
	CHECK(declarations[0].EndLine == 21);
	CHECK(declarations[1].Line == 4);
	CHECK(declarations[1].EndLine == 10);
	CHECK(declarations[2].Line == 13);
	CHECK(declarations[2].EndLine == 16);
}

TEST_CASE("An edit below a declaration leaves it where it was")
{
	std::vector<Declaration> declarations = {Spanning("Draw", DeclarationKind::Function, 4, 9)};

	MoveDeclarations(declarations, 10, 5);

	CHECK(declarations[0].Line == 4);
	CHECK(declarations[0].EndLine == 9);
}

TEST_CASE("A declaration whose first line is deleted is dropped and what it held takes its parent")
{
	std::vector<Declaration> declarations = {Spanning("Renderer", DeclarationKind::Type, 2, 20),
											 Spanning("Inner", DeclarationKind::Type, 4, 16, kFirstDeclaration),
											 Spanning("Draw", DeclarationKind::Function, 8, 10, kSecondDeclaration)};

	MoveDeclarations(declarations, 3, -2);

	REQUIRE(declarations.size() == 2);
	CHECK(declarations[0].Name == "Renderer");
	CHECK(declarations[0].EndLine == 18);
	CHECK(declarations[1].Name == "Draw");
	CHECK(declarations[1].Line == 6);
	CHECK(declarations[1].Parent == kFirstDeclaration);
}

TEST_CASE("A declaration whose whole range is deleted is dropped")
{
	std::vector<Declaration> declarations = RendererWithTwoMethods();

	MoveDeclarations(declarations, 11, -5);

	REQUIRE(declarations.size() == 2);
	CHECK(declarations[1].Name == "Draw");
	CHECK(declarations[0].EndLine == 15);
}

TEST_CASE("A deletion that takes a declaration's last line ends the declaration where the deletion began")
{
	std::vector<Declaration> declarations = {Spanning("Draw", DeclarationKind::Function, 4, 9)};

	MoveDeclarations(declarations, 7, -5);

	REQUIRE(declarations.size() == 1);
	CHECK(declarations[0].Line == 4);
	CHECK(declarations[0].EndLine == 6);
}

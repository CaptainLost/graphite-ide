#include "Language/Api/Declarations.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		bool Contains(const Declaration& declaration, std::size_t line)
		{
			return declaration.Line <= line && line <= declaration.EndLine;
		}

		bool LiesWithin(const Declaration& inner, const Declaration& outer)
		{
			return inner.Line > outer.Line || (inner.Line == outer.Line && inner.EndLine <= outer.EndLine);
		}

		std::optional<std::size_t> EarlierParentOf(std::span<const Declaration> declarations, std::size_t index)
		{
			const int parent = declarations[index].Parent;

			if (parent < 0 || static_cast<std::size_t>(parent) >= index)
			{
				return std::nullopt;
			}

			return static_cast<std::size_t>(parent);
		}

		std::optional<std::size_t> MovedLine(std::size_t line, std::size_t fromLine, std::ptrdiff_t delta)
		{
			if (line < fromLine)
			{
				return line;
			}

			const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(line) + delta;

			if (moved < static_cast<std::ptrdiff_t>(fromLine))
			{
				return std::nullopt;
			}

			return static_cast<std::size_t>(moved);
		}

		std::size_t MovedLineWithin(std::size_t line, std::size_t fromLine, std::ptrdiff_t delta,
									std::size_t movedStart)
		{
			return std::max(movedStart, LineAfterEdit(line, fromLine, delta));
		}

		int SurvivingParent(std::span<const Declaration> declarations, const std::vector<int>& movedIndices,
							std::size_t index)
		{
			std::optional<std::size_t> parent = EarlierParentOf(declarations, index);

			while (parent && movedIndices[*parent] == kNoParentDeclaration)
			{
				parent = EarlierParentOf(declarations, *parent);
			}

			return parent ? movedIndices[*parent] : kNoParentDeclaration;
		}
	}

	std::vector<const Declaration*> InnermostDeclarationAt(std::span<const Declaration> declarations,
														   std::size_t line)
	{
		std::optional<std::size_t> innermost;

		for (std::size_t index = 0; index < declarations.size(); ++index)
		{
			const bool deeper = !innermost || LiesWithin(declarations[index], declarations[*innermost]);

			if (Contains(declarations[index], line) && deeper)
			{
				innermost = index;
			}
		}

		std::vector<const Declaration*> chain;

		for (std::optional<std::size_t> link = innermost; link; link = EarlierParentOf(declarations, *link))
		{
			chain.push_back(&declarations[*link]);
		}

		std::reverse(chain.begin(), chain.end());

		return chain;
	}

	std::size_t LineAfterEdit(std::size_t line, std::size_t fromLine, std::ptrdiff_t delta)
	{
		const std::size_t lineBeforeTheDeletion = fromLine > 0 ? fromLine - 1 : 0;

		return MovedLine(line, fromLine, delta).value_or(lineBeforeTheDeletion);
	}

	void MoveDeclarations(std::vector<Declaration>& declarations, std::size_t fromLine, std::ptrdiff_t delta)
	{
		if (delta == 0)
		{
			return;
		}

		std::vector<int> movedIndices(declarations.size(), kNoParentDeclaration);
		std::vector<Declaration> moved;
		moved.reserve(declarations.size());

		for (std::size_t index = 0; index < declarations.size(); ++index)
		{
			const Declaration& declaration = declarations[index];
			const std::optional<std::size_t> start = MovedLine(declaration.Line, fromLine, delta);

			if (!start)
			{
				continue;
			}

			Declaration next = declaration;
			next.Line = *start;
			next.NameLine = MovedLineWithin(declaration.NameLine, fromLine, delta, *start);
			next.EndLine = MovedLineWithin(declaration.EndLine, fromLine, delta, *start);
			next.Parent = SurvivingParent(declarations, movedIndices, index);

			movedIndices[index] = static_cast<int>(moved.size());
			moved.push_back(std::move(next));
		}

		declarations = std::move(moved);
	}
}

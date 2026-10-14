#pragma once

#include <string>
#include <vector>

namespace Graphite {

	struct HoverInfo
	{
		std::string Kind;
		std::string Name;
		std::string Scope;
		std::string Access;
		std::vector<std::string> Qualifiers;
		std::string Declaration;
		std::string OverloadNote;
		std::string ReturnType;
		std::vector<std::string> Parameters;
		std::string Type;
		std::string TypeAka;
		std::string Value;
		std::string Offset;
		std::string Size;
		std::string Padding;
		std::string Alignment;
		std::string Documentation;
		std::string Raw;

		bool Parsed = false;
	};
}

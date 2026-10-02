#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// What clangd's hover markdown says about a symbol, split into the fields a tooltip draws.
	struct HoverInfo
	{
		std::string Kind;            // "instance method", "field", "class"... — hyphens shown as spaces
		std::string Name;
		std::string Scope;           // "Layer", "namespace Core", "Layer::OnUpdate"
		std::string Access;          // "public", "private", "protected", or empty
		std::vector<std::string> Qualifiers; // "virtual", "static", "const", in declaration order
		std::string Declaration;     // the code block's declaration line, access prefix removed
		std::string ReturnType;
		std::vector<std::string> Parameters;
		std::string Type;
		std::string TypeAka;
		std::string Value;
		std::string Offset;          // "8 bytes"
		std::string Size;            // "1 byte"
		std::string Padding;         // "7 bytes"
		std::string Alignment;       // "1 byte"
		std::string Documentation;   // paragraphs joined by "\n\n"
		std::string Raw;             // the markdown as received

		// False when not even the "### " heading was found: the tooltip then shows Raw as text.
		bool Parsed = false;
	};

	HoverInfo ParseClangdHover(std::string_view markdown);
}

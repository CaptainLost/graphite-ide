#pragma once

#include "Lang/Api/LanguageService.h"

#include <cstdint>
#include <vector>

namespace Core {

	class DocumentSet;
}

namespace Graphite {

	enum class DefinitionOutcome : std::uint8_t
	{
		None,
		Jumped,
		NeedsChoice
	};

	// Applies a definition reply: nothing, a jump, or a list to choose from. The chosen
	// location is applied through the same Jump below.
	//
	// Deduplicates on (Path, Line, Column) first - two locations naming the same place are one
	// choice, not a pointless one between identical answers - and then drops any location equal
	// to `from`: asking for the definition of a definition should say there is nowhere to go,
	// not silently jump to where the caret already sits.
	//
	// `candidates` is filled with what survives both steps in every outcome, not only
	// NeedsChoice: the single survivor of a Jumped answer sits in it too, so a caller applying
	// the reply never has to branch on which outcome it got before it can read the location out.
	DefinitionOutcome ResolveDefinition(const std::vector<Location>& locations,
										const FilePosition& from,
										std::vector<Location>& candidates);

	// Opens the location's file permanently, not as a preview - a definition is somewhere you
	// meant to go, unlike a hit you are skimming - and places the caret there, converting the
	// location's UTF-16 column back to a byte offset through the buffer now in hand. Raises
	// OpenItem::PendingScroll, because from the editor's own perspective this frame the caret
	// did not move during its own processing.
	void Jump(Core::DocumentSet& documents, const Location& location);
}

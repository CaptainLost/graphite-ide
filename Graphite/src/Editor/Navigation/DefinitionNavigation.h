#pragma once

#include "Editor/Navigation/SelectInDocument.h"
#include "Language/Api/LanguageService.h"

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

	// WARNING: `candidates` is filled with the survivors in every outcome, not only NeedsChoice,
	// so the single survivor of a Jumped answer can be read from it too.
	DefinitionOutcome ResolveDefinition(const std::vector<Location>& locations,
										const FilePosition& from,
										std::vector<Location>& candidates);

	void Jump(Core::DocumentSet& documents, const Location& location, const OpenDocument& open);
}

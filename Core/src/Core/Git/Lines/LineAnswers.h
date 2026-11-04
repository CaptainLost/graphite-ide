#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	// What the user decided for one repository; every name is a full reference name.
	struct LineAnswers
	{
		std::vector<std::string> AddedBases;
		std::vector<std::string> RemovedBases;
		std::vector<std::string> KeptBases;  // found once by the rules; they stay bases after their line stops qualifying
		std::vector<std::string> BaseOrder;  // every name of AddedBases and KeptBases, once, in the order each first became a base
		std::optional<std::string> MainLine; // the answer to "which branch is the main line?"

		// Branch -> what it is compared with; an empty value means nothing (the branch on its own).
		std::map<std::string, std::optional<std::string>> Comparisons;

		friend bool operator==(const LineAnswers&, const LineAnswers&) = default;
	};
}

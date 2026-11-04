#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineAnswerChange.h"
#include "Git/Lines/LineReader.h"

#include "Core/Git/Lines/LineQuestions.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kMostOtherBranches = 50;

	inline constexpr const char* kVsBasesHeading = "Bases in this repo, found from the data";

	struct VsOption
	{
		enum class Kind
		{
			BuiltOn,
			Base,
			Branch,
			AnotherBranch,
			Nothing
		};

		Kind What = Kind::Base;
		std::optional<std::size_t> Ref; // into LineRead::Refs
		std::string FullName;           // of the ref; empty for AnotherBranch and Nothing
		std::string Name;               // short name, remote prefix kept
		std::string Why;                // "the branch it is built on", "forked 3d ago · on its line", "forked 3d ago · via a merge", "no commit in common", "…"
		std::string Counts;             // "6 ahead · 80 behind", "…" pending, empty for Nothing
		bool Automatic = false;
		bool Chosen = false;
		bool Guessed = false;
	};

	struct VsBaseLine
	{
		std::string Name;
		std::string Reason;
		bool OldCopy = false;
	};

	struct VsPicker
	{
		std::string Title;   // "Compare <subject> with"; empty when the branch has no place in the read
		std::string Subject; // the name inside the title
		std::vector<VsOption> Options;
		std::vector<VsBaseLine> Bases;
		std::size_t QuietBases = 0;
		std::string Footer;
	};

	struct VsPickerInput
	{
		const LineRead& Read;
		const BranchLines& Lines;
		std::string_view Branch; // full name
		std::string_view Search;

		// How far the branch is from the ref at the index: empty while asked.
		const std::function<std::optional<Core::LineDistance>(std::size_t ref)>& DistanceTo;

		// How far an old copy is behind the base it is a copy of: empty while asked.
		const std::function<std::optional<Core::LineDistance>(const Core::OldCopy& copy)>& OldCopyDistance;

		std::chrono::sys_seconds Now;
	};

	VsPicker BuildVsPicker(const VsPickerInput& input);

	// Empty for the search row, which is no choice.
	std::optional<LineAnswerChange> ChangeFor(const VsPicker&, const VsOption& chosen, std::string_view branch);
}

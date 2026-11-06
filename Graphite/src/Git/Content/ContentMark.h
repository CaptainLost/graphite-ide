#pragma once

#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Oid.h"

#include <optional>

namespace Graphite {

	// What is known about whether a branch is merged into its line by its contents.
	struct ContentMark
	{
		enum class State
		{
			Pending, // not checked yet, or the check runs
			Known,
			Unknown, // the check could not answer: an object it needs is missing, or it failed
		} State = State::Pending;

		std::optional<Core::ContentVerdict> Verdict; // when Known
	};

	// Whether a line holds a copy of a commit: a commit carrying the same patch.
	struct TwinMark
	{
		enum ContentMark::State State = ContentMark::State::Pending;
		std::optional<Core::Oid> Twin; // when Known and the line holds one
	};

}

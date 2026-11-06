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

	// Whether a mark holds back what is drawn with it: it is pending, and the model is able to answer. The model checks
	// nothing while the window is off screen, nor for a line of a read it does not follow, so a wait on those would never
	// end; an unknown or failed answer is an answer.
	inline bool ContentCheckWaits(const ContentMark& mark, bool modelChecks)
	{
		return modelChecks && mark.State == ContentMark::State::Pending;
	}

	// What the shown branch's content mark asks of a frame. The view's read is the model's read except in the one frame a
	// new read lands (the sidebar is rebuilt later in it); that frame asks nothing, since the line belongs to the read
	// before, and counts as waiting, so what waits for the mark does not let go a frame early.
	enum class ShownContentStep
	{
		NoWait,      // not a candidate, or the window is not drawn
		WaitForRead, // the read just landed: wait, and ask the model next frame
		AskModel,    // ask the model and wait while its mark is pending
	};

	inline ShownContentStep StepForShownContent(bool isCandidate, bool drawn, bool viewFollowsModelsRead)
	{
		if (!isCandidate)
		{
			return ShownContentStep::NoWait;
		}

		if (!viewFollowsModelsRead)
		{
			return drawn ? ShownContentStep::WaitForRead : ShownContentStep::NoWait;
		}

		return ShownContentStep::AskModel;
	}

}

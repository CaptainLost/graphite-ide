#pragma once

#include "Core/Base/Base.h"

#include "Review/ReviewSessions.h"

#include "Core/Git/Ref.h"

#include <string>
#include <vector>

namespace Graphite {

	enum class ReviewAction
	{
		Start,
		Open,
		End
	};

	struct ReviewMenuItem
	{
		std::string Label;
		ReviewAction Action = ReviewAction::Start;
		std::string Branch;

		// The base a Start item starts against; empty for Open and End.
		std::string Base;
	};

	struct ReviewMenu
	{
		std::vector<ReviewMenuItem> Items;

		std::string Submenu;
		std::vector<ReviewMenuItem> SubmenuItems;
	};

	ReviewMenu ReviewMenuFor(const Core::Branch& branch, const std::vector<Core::Branch>& branches,
							 const ReviewSessions& sessions);
}

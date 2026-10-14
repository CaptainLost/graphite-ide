#pragma once

#include "Core/Base/Base.h"

#include "Review/ReviewFiles.h"
#include "Review/ReviewSessions.h"

#include "Core/Base/Result.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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

		// What a Start reviews the branch against; empty for Open and End.
		std::string Base;
	};

	// What a branch's menu offers about reviewing it.
	struct ReviewMenu
	{
		std::vector<ReviewMenuItem> Items;

		// With HEAD detached there is no default base: the items are the bases to pick from, drawn
		// as a submenu under this label. Empty when the items are drawn as they are.
		std::string Submenu;
	};

	// A branch under review offers to open its review and to end it. Any other is offered a review
	// against the checked-out branch - or, for the checked-out branch itself, against its upstream,
	// and nothing without one. With HEAD detached, a submenu of the other local branches.
	ReviewMenu ReviewMenuFor(const Core::Branch& branch, const std::vector<Core::Branch>& branches,
							 const ReviewSessions& sessions);

	// Beside a branch under review: the committed range's viewed files out of all of them, merged once
	// every commit is in the base, a question mark when the range has a problem or could not be read,
	// and the bare mark while the range or its diff is on its way. Empty for a branch without a session.
	// `rangeError` is null unless the last read of the range failed; it matters only without a range.
	std::string ReviewBadgeText(const ReviewSessions& sessions, const std::string& branch, const ReviewRange* range,
								const Core::Error* rangeError, const Core::Diff* committed);

	// The badges last worked out, by branch: counting a large range's viewed files on every frame
	// would cost the frame. A badge is worked out again when its range, its read, its diff or the
	// sessions change.
	class ReviewBadges
	{
	public:
		std::string TextFor(const ReviewSessions& sessions, const std::string& branch,
							const Core::Ref<const ReviewRange>& range, const Core::Error* rangeError,
							const Core::Ref<const Core::Diff>& committed);

		// The text held for this range and these sessions when it waits for nothing - its range is
		// held, and the diff it counts, if it counts one, had arrived - so the caller need not ask
		// for the diff at all.
		std::optional<std::string> HeldFor(const ReviewSessions& sessions, const std::string& branch,
										   const Core::Ref<const ReviewRange>& range) const;

		// Everything held is about the folder that was open.
		void Clear() { m_Badges.clear(); }

	private:
		struct HeldBadge
		{
			Core::Ref<const ReviewRange> Range;
			bool RangeFailed = false;
			Core::Ref<const Core::Diff> Committed;
			std::uint64_t SessionsVersion = 0;
			std::string Text;
		};

		std::unordered_map<std::string, HeldBadge> m_Badges;
	};
}

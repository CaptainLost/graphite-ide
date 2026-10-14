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

		std::string Base;
	};

	struct ReviewMenu
	{
		std::vector<ReviewMenuItem> Items;

		std::string Submenu;
	};

	ReviewMenu ReviewMenuFor(const Core::Branch& branch, const std::vector<Core::Branch>& branches,
							 const ReviewSessions& sessions);

	std::string ReviewBadgeText(const ReviewSessions& sessions, const std::string& branch, const ReviewRange* range,
								const Core::Error* rangeError, const Core::Diff* committed);

	// WARNING: Counting a large range's viewed files on every frame would cost the frame, so badges are held by
	// branch. Clear them when the open folder changes: everything held is about the folder that was open.
	class ReviewBadges
	{
	public:
		std::string TextFor(const ReviewSessions& sessions, const std::string& branch,
							const Core::Ref<const ReviewRange>& range, const Core::Error* rangeError,
							const Core::Ref<const Core::Diff>& committed);

		std::optional<std::string> HeldFor(const ReviewSessions& sessions, const std::string& branch,
										   const Core::Ref<const ReviewRange>& range) const;

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

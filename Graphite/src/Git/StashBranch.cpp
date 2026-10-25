#include "Git/StashBranch.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"
#include "Core/Text/Encoding.h"
#include "UI/TypographicMarks.h"

#include <array>
#include <format>

namespace Graphite {

	std::optional<std::string> StashBranch(std::string_view message)
	{
		constexpr std::array<std::string_view, 2> prefixes{"WIP on ", "On "};
		constexpr std::string_view detachedHeadBranch = "(no branch)";

		for (const std::string_view prefix : prefixes)
		{
			if (!message.starts_with(prefix))
			{
				continue;
			}

			const std::string_view afterPrefix = message.substr(prefix.size());

			if (afterPrefix.starts_with(detachedHeadBranch) &&
				afterPrefix.substr(detachedHeadBranch.size()).starts_with(':'))
			{
				return std::string(detachedHeadBranch);
			}

			const std::size_t colonPosition = afterPrefix.find(':');
			const bool hasNoBranchBeforeColon = colonPosition == std::string_view::npos || colonPosition == 0;

			if (hasNoBranchBeforeColon || afterPrefix.substr(0, colonPosition).find(' ') != std::string_view::npos)
			{
				return std::nullopt;
			}

			return std::string(afterPrefix.substr(0, colonPosition));
		}

		return std::nullopt;
	}

	StashLabel StashRowLabel(std::string_view message, std::string_view when)
	{
		message = Core::FirstLine(message);

		const std::optional<std::string> branch = StashBranch(message);

		if (branch.has_value() && message.starts_with("On "))
		{
			constexpr std::string_view blankCharacters = " \t\r";
			const std::string_view readerMessage =
				Core::WithoutSurrounding(message.substr(message.find(':') + 1), blankCharacters);

			if (!readerMessage.empty())
			{
				return StashLabel{.Text = std::string(readerMessage), .Named = true};
			}
		}
		else if (!branch.has_value() && !message.empty())
		{
			return StashLabel{.Text = std::string(message), .Named = true};
		}

		return StashLabel{.Text = branch.has_value() ? std::format("{} - {}", when, *branch) : std::string(when),
						  .Named = false};
	}

	std::string DropStashSubject(std::size_t index, std::string_view message, std::size_t maxChars)
	{
		const StashLabel label = StashRowLabel(message, std::string_view{});
		const std::string position = std::format("stash@{{{}}}", index);

		if (!label.Named)
		{
			return position;
		}

		std::size_t cutOffset = label.Text.size();
		std::size_t keptCharacters = 0;

		for (std::size_t byteIndex = 0; byteIndex < label.Text.size(); ++byteIndex)
		{
			if (Core::IsUtf8ContinuationByte(label.Text[byteIndex]))
			{
				continue;
			}

			if (keptCharacters == maxChars)
			{
				cutOffset = byteIndex;
				break;
			}

			++keptCharacters;
		}

		const std::string_view shown = std::string_view(label.Text).substr(0, cutOffset);
		const std::string_view truncationSuffix = cutOffset < label.Text.size() ? kEllipsis : std::string_view{};

		return std::format("\"{}{}\" ({})", shown, truncationSuffix, position);
	}

	std::string StashSectionCount(std::optional<std::size_t> stashCount)
	{
		if (!stashCount.has_value())
		{
			return {};
		}

		return std::format("({})", *stashCount);
	}
}

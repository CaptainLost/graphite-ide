#include "Git/StashBranch.h"

#include <array>
#include <format>

namespace Graphite {

	std::optional<std::string> StashBranch(std::string_view message)
	{
		constexpr std::array<std::string_view, 2> prefixes{"WIP on ", "On "};

		for (const std::string_view prefix : prefixes)
		{
			if (!message.starts_with(prefix))
			{
				continue;
			}

			const std::string_view rest = message.substr(prefix.size());

			// What git writes in place of a branch for a stash made on a detached head - the
			// one name here with a space in it.
			constexpr std::string_view detached = "(no branch)";

			if (rest.starts_with(detached) && rest.substr(detached.size()).starts_with(':'))
			{
				return std::string(detached);
			}

			const std::size_t colon = rest.find(':');

			// A branch name has no spaces either, so one before the colon means the colon
			// belongs to a message that only happens to start like git's.
			if (colon == std::string_view::npos || colon == 0 ||
				rest.substr(0, colon).find(' ') != std::string_view::npos)
			{
				return std::nullopt;
			}

			return std::string(rest.substr(0, colon));
		}

		return std::nullopt;
	}

	StashLabel StashRowLabel(std::string_view message, std::string_view when)
	{
		message = message.substr(0, message.find('\n'));

		const std::optional<std::string> branch = StashBranch(message);

		if (branch.has_value() && message.starts_with("On "))
		{
			// After the branch's colon, which is the first: neither a branch name nor git's
			// "(no branch)" can hold one.
			std::string_view own = message.substr(message.find(':') + 1);

			// A message written on Windows can end its line in a carriage return.
			constexpr std::string_view blank = " \t\r";
			const std::size_t first = own.find_first_not_of(blank);
			own = first == std::string_view::npos ? std::string_view{}
												  : own.substr(first, own.find_last_not_of(blank) - first + 1);

			if (!own.empty())
			{
				return StashLabel{.Text = std::string(own), .Named = true};
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
		const StashLabel label = StashRowLabel(message, {});
		const std::string position = std::format("stash@{{{}}}", index);

		if (!label.Named)
		{
			return position;
		}

		// Cut after maxChars characters: a byte that is not a continuation byte starts one.
		std::size_t end = label.Text.size();
		std::size_t chars = 0;

		for (std::size_t at = 0; at < label.Text.size(); ++at)
		{
			if ((static_cast<unsigned char>(label.Text[at]) & 0xC0) == 0x80)
			{
				continue;
			}

			if (chars == maxChars)
			{
				end = at;
				break;
			}

			++chars;
		}

		const std::string_view shown = std::string_view(label.Text).substr(0, end);
		const std::string_view ellipsis = end < label.Text.size() ? "\xE2\x80\xA6" : "";

		return std::format("\"{}{}\" ({})", shown, ellipsis, position);
	}
}

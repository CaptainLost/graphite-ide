#include "Sessions/Overview/SessionPlace.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kClaudeFolder = ".claude";
		constexpr std::string_view kWorktreesFolder = "worktrees";
		constexpr std::string_view kAgentPrefix = "agent-";
		constexpr std::size_t kWorktreeComponentsAfterClaude = 3;

		bool StartsWithIgnoringCase(std::string_view text, std::string_view prefix)
		{
			return text.size() >= prefix.size() && Core::EqualsIgnoringCase(text.substr(0, prefix.size()), prefix);
		}
	}

	SessionPlace PlaceOfDirectory(const std::filesystem::path& cwd)
	{
		std::vector<std::filesystem::path> parts(cwd.begin(), cwd.end());

		for (std::size_t index = 0; index + kWorktreeComponentsAfterClaude <= parts.size(); ++index)
		{
			if (!Core::EqualsIgnoringCase(Core::ToUtf8(parts[index]), kClaudeFolder) ||
				!Core::EqualsIgnoringCase(Core::ToUtf8(parts[index + 1]), kWorktreesFolder))
			{
				continue;
			}

			std::filesystem::path repository;
			for (std::size_t before = 0; before < index; ++before)
			{
				repository /= parts[before];
			}

			std::string name = Core::ToUtf8(parts[index + 2]);

			if (repository.empty() || name.empty() || StartsWithIgnoringCase(name, kAgentPrefix))
			{
				name.clear();
			}

			return {std::move(repository), std::move(name)};
		}

		return {cwd, {}};
	}

	SessionPlace PlaceOfSession(const SessionSummary& summary, const Pricing& pricing)
	{
		const std::string* bestByCost = nullptr;
		const std::string* bestByTokens = nullptr;
		double bestDollars = 0.0;
		std::uint64_t bestTokens = 0;

		for (const auto& [cwd, usage] : summary.Activity.MainUsageByCwd)
		{
			// WARNING: Records without a cwd collect under the empty key, which names no place.
			if (cwd.empty())
			{
				continue;
			}

			const double dollars = pricing.CostOf(usage).Dollars;
			if (dollars > bestDollars)
			{
				bestDollars = dollars;
				bestByCost = &cwd;
			}

			std::uint64_t tokens = 0;
			for (const auto& [key, counts] : usage)
			{
				tokens += counts.Total();
			}

			if (bestByTokens == nullptr || tokens > bestTokens)
			{
				bestTokens = tokens;
				bestByTokens = &cwd;
			}
		}

		const std::string* chosen = bestByCost != nullptr ? bestByCost : bestByTokens;

		if (chosen == nullptr)
		{
			return PlaceOfDirectory(summary.Cwd);
		}

		return PlaceOfDirectory(Core::FromUtf8(*chosen));
	}
}

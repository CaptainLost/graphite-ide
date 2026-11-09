#include "Git/History/HistoryFilter.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <string>
#include <utility>

namespace Graphite {

	Core::LogRequest BuildLogRequest(const std::optional<std::string>& ref,
									 const HistoryFilter& filter,
									 const HistoryReach& reach,
									 const std::optional<Core::Oid>& stash, bool firstParentsOnly)
	{
		Core::LogRequest request;
		request.Ref = ref;
		request.Stash = stash;
		request.Filter.Text = filter.Text;

		const std::string trimmedPath = Core::Trimmed(filter.Path);

		// WARNING: A path of only whitespace must leave File unset. Narrowing by file diffs every
		// commit the walk passes, which makes the plain branch log the most expensive query there is.
		if (!trimmedPath.empty())
		{
			request.File = Core::FromUtf8(trimmedPath);
		}

		// A stash is one commit and a file's history follows that file through merges, so neither is cut to first parents.
		request.FirstParentOnly = firstParentsOnly && !request.Stash.has_value() && !request.File.has_value();

		request.MaxCount = reach.MaxCount;
		request.MaxWalk = reach.MaxWalk;

		return request;
	}

	bool NarrowsHistory(const Core::LogRequest& request)
	{
		return !Core::Trimmed(request.Filter.Text).empty() || request.File.has_value();
	}

	bool AnswersAnotherHistory(const Core::LogRequest& shown, const Core::LogRequest& asked)
	{
		return shown.Ref != asked.Ref || shown.Stash != asked.Stash || shown.From != asked.From;
	}

	void SettlingFilter::Set(HistoryFilter filter)
	{
		if (filter == m_Pending)
		{
			return;
		}

		m_Pending = std::move(filter);
		m_Since = 0.0f;
		m_JustSet = true;
	}

	bool SettlingFilter::Advance(Core::Timestep step)
	{
		const float seconds = step.GetSeconds();

		if (m_Pending == m_Requested)
		{
			return false;
		}

		constexpr bool kChangeFlagCleared = false;
		const bool changeWasMadeThisFrame = std::exchange(m_JustSet, kChangeFlagCleared);

		if (!changeWasMadeThisFrame)
		{
			m_Since += seconds;
		}

		if (m_Since < kSettleSeconds)
		{
			return false;
		}

		m_Requested = m_Pending;
		return true;
	}

	void SettlingFilter::Accept()
	{
		m_Requested = m_Pending;
		m_Since = 0.0f;
		m_JustSet = false;
	}

	void SettlingFilter::Reset()
	{
		m_Pending = HistoryFilter{};
		m_Requested = HistoryFilter{};
		m_Since = 0.0f;
		m_JustSet = false;
	}
}

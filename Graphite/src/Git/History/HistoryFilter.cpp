#include "Git/History/HistoryFilter.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <string>
#include <utility>

namespace Graphite {

	Core::LogRequest BuildLogRequest(const std::optional<std::string>& ref,
									 const HistoryFilter& filter,
									 const HistoryReach& reach,
									 const std::optional<Core::Oid>& stash)
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

		request.MaxCount = reach.MaxCount;
		request.MaxWalk = reach.MaxWalk;

		return request;
	}

	bool NarrowsHistory(const HistoryFilter& filter)
	{
		return !filter.Text.empty() || !Core::Trimmed(filter.Path).empty();
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

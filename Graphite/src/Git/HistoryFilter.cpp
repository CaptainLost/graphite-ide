#include "Git/HistoryFilter.h"

#include "Core/Base/Ascii.h"

#include <filesystem>
#include <utility>
#include <string>

namespace Graphite {

	Core::LogRequest BuildLogRequest(const std::optional<std::string>& ref,
									 const HistoryFilter& filter,
									 std::optional<std::size_t> maxCount,
									 const std::optional<Core::Oid>& stash)
	{
		Core::LogRequest request;
		request.Ref = ref;
		request.Stash = stash;
		request.Filter.Text = filter.Text;

		if (const std::string path = Core::Trimmed(filter.Path); !path.empty())
		{
			request.File = std::filesystem::path{path};
		}

		if (maxCount.has_value())
		{
			request.MaxCount = *maxCount;
		}

		return request;
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

	bool SettlingFilter::Advance(float seconds)
	{
		if (m_Pending == m_Requested)
		{
			return false;
		}

		// The frame the change was made in does not count: the frame loop sleeps when idle,
		// so the step the keystroke's frame carries is the whole sleep before it, and
		// counting it would settle the first letter typed at once.
		if (!std::exchange(m_JustSet, false))
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

	bool MatchesRefFilter(std::string_view name, std::string_view text)
	{
		const std::string wanted = Core::Lowered(Core::Trimmed(text));

		if (wanted.empty())
		{
			return true;
		}

		return Core::Lowered(name).find(wanted) != std::string::npos;
	}
}

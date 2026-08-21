#include "Git/HistoryFilter.h"

#include "Core/Base/Ascii.h"

#include <filesystem>
#include <utility>
#include <string>

namespace Graphite {

	Core::LogRequest BuildLogRequest(const std::optional<std::string>& ref,
									 const HistoryFilter& filter,
									 std::optional<std::size_t> maxCount)
	{
		Core::LogRequest request;
		request.Ref = ref;
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
	}

	bool SettlingFilter::Advance(float seconds)
	{
		if (m_Pending == m_Requested)
		{
			return false;
		}

		m_Since += seconds;

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
	}

	void SettlingFilter::Reset()
	{
		m_Pending = HistoryFilter{};
		m_Requested = HistoryFilter{};
		m_Since = 0.0f;
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

#pragma once

#include "Core/Base/Timestep.h"

#include "Core/Git/Requests.h"
#include "Git/History/HistoryReach.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	struct HistoryFilter
	{
		std::string Text;
		std::string Path;

		friend bool operator==(const HistoryFilter&, const HistoryFilter&) = default;
	};

	class SettlingFilter
	{
	public:
		static constexpr float kSettleSeconds = 0.2f;

		void Set(HistoryFilter filter);
		bool Advance(Core::Timestep step);
		const HistoryFilter& Get() const { return m_Pending; }
		void Accept();
		void Reset();

	private:
		HistoryFilter m_Pending;
		HistoryFilter m_Requested;
		float m_Since = 0.0f;
		bool m_JustSet = false;
	};

	Core::LogRequest BuildLogRequest(const std::optional<std::string>& ref,
									 const HistoryFilter& filter,
									 const HistoryReach& reach,
									 const std::optional<Core::Oid>& stash = std::nullopt);
}

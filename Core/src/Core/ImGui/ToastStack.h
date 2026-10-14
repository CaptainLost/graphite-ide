#pragma once

#include "Core/Base/Timestep.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	enum class ToastKind
	{
		Success,
		Warning,
		Error
	};

	enum class ToastDepartureReason
	{
		ClosedByUser,
		Expired,
		Cleared
	};

	struct ToastDeparture
	{
		std::uint64_t Id = 0;
		ToastDepartureReason Reason = ToastDepartureReason::ClosedByUser;
		bool WasHovered = false;
	};

	struct Toast
	{
		std::uint64_t Id = 0;
		ToastKind Kind = ToastKind::Error;

		std::string Title;
		std::string Message;
		std::string CopyText;

		std::uint32_t Group = 0;

		int Count = 1;

		SteadyClock::time_point Shown;

		std::optional<SteadyClock::time_point> LeavingSince;

		SteadyClock::duration Left{};
		std::optional<SteadyClock::time_point> CountingFrom;
		bool WasHovered = false;
	};

	class ToastStack
	{
	public:
		using Clock = SteadyClock;

		static constexpr Clock::duration kErrorLifetime = std::chrono::seconds(12);
		static constexpr Clock::duration kWarningAndSuccessLifetime = std::chrono::seconds(6);
		static constexpr Clock::duration kEnter = std::chrono::milliseconds(150);
		static constexpr Clock::duration kLeave = std::chrono::milliseconds(150);

		std::uint64_t Show(ToastKind kind, std::string title, std::string message, Clock::time_point now,
						   std::string copyText = {}, std::uint32_t group = 0);

		void SetHovered(std::uint64_t id, bool hovered, Clock::time_point now);

		void Close(std::uint64_t id, Clock::time_point now);

		void Update(Clock::time_point now);

		void Clear();

		void Clear(std::uint32_t group);

		const std::vector<Toast>& Get() const { return m_Toasts; }

		std::vector<ToastDeparture> TakeDepartures();

		std::optional<Clock::time_point> NextChange(Clock::time_point now) const;

		bool IsAnyCountdownRunning() const;

		static float ShareOfLifetimeLeft(const Toast& toast, Clock::time_point now);

		static Clock::duration LifetimeOf(ToastKind kind);

	private:
		std::vector<Toast> m_Toasts;
		std::uint64_t m_NextId = 1;
		std::vector<ToastDeparture> m_Departures;

		void NoteDeparture(const Toast& toast, ToastDepartureReason reason);
	};
}

#include "corepch.h"

#include "Core/ImGui/ToastStack.h"

#include <algorithm>
#include <utility>

namespace Core {

	namespace {
		Toast* FindToast(std::vector<Toast>& toasts, std::uint64_t id)
		{
			const std::vector<Toast>::iterator found = std::ranges::find(toasts, id, &Toast::Id);
			return found != toasts.end() ? &*found : nullptr;
		}

		Toast* FindTwin(std::vector<Toast>& toasts, ToastKind kind, const std::string& title, const std::string& message,
						std::uint32_t group)
		{
			for (Toast& toast : toasts)
			{
				const bool isTwin = !toast.LeavingSince.has_value() && toast.Kind == kind && toast.Title == title &&
									toast.Message == message && toast.Group == group;
				if (isTwin)
				{
					return &toast;
				}
			}

			return nullptr;
		}

		bool HasFinishedLeaving(const Toast& toast, ToastStack::Clock::time_point now)
		{
			return toast.LeavingSince.has_value() && now >= *toast.LeavingSince + ToastStack::kLeave;
		}
	}

	std::uint64_t ToastStack::Show(ToastKind kind, std::string title, std::string message, Clock::time_point now,
								   std::string copyText, std::uint32_t group)
	{
		Toast* const twin = FindTwin(m_Toasts, kind, title, message, group);

		if (twin != nullptr)
		{
			++twin->Count;
			twin->Left = LifetimeOf(kind);

			const bool twinIsCountingDown = twin->CountingFrom.has_value();
			if (twinIsCountingDown)
			{
				twin->CountingFrom = now;
			}

			return twin->Id;
		}

		Toast toast;
		toast.Id = m_NextId++;
		toast.Kind = kind;
		toast.Title = std::move(title);
		toast.Message = std::move(message);
		toast.CopyText = std::move(copyText);
		toast.Group = group;
		toast.Shown = now;
		toast.Left = LifetimeOf(kind);
		toast.CountingFrom = now;

		const std::uint64_t shownId = toast.Id;
		m_Toasts.push_back(std::move(toast));
		return shownId;
	}

	void ToastStack::Clear()
	{
		for (const Toast& toast : m_Toasts)
		{
			const bool alreadyReported = toast.LeavingSince.has_value();
			if (!alreadyReported)
			{
				NoteDeparture(toast, ToastDepartureReason::Cleared);
			}
		}

		m_Toasts.clear();
	}

	void ToastStack::Clear(std::uint32_t group)
	{
		for (const Toast& toast : m_Toasts)
		{
			const bool clearedNow = toast.Group == group && !toast.LeavingSince.has_value();
			if (clearedNow)
			{
				NoteDeparture(toast, ToastDepartureReason::Cleared);
			}
		}

		std::erase_if(m_Toasts, [group](const Toast& toast) { return toast.Group == group; });
	}

	std::vector<ToastDeparture> ToastStack::TakeDepartures()
	{
		std::vector<ToastDeparture> noDepartures;
		return std::exchange(m_Departures, std::move(noDepartures));
	}

	void ToastStack::NoteDeparture(const Toast& toast, ToastDepartureReason reason)
	{
		m_Departures.push_back({.Id = toast.Id, .Reason = reason, .WasHovered = toast.WasHovered});
	}

	void ToastStack::SetHovered(std::uint64_t id, bool hovered, Clock::time_point now)
	{
		Toast* const toast = FindToast(m_Toasts, id);

		if (toast == nullptr || toast->LeavingSince.has_value())
		{
			return;
		}

		if (hovered)
		{
			toast->WasHovered = true;
		}

		const bool paused = !toast->CountingFrom.has_value();

		if (hovered && !paused)
		{
			toast->Left -= now - *toast->CountingFrom;
			toast->CountingFrom.reset();
		}
		else if (!hovered && paused)
		{
			toast->CountingFrom = now;
		}
	}

	void ToastStack::Close(std::uint64_t id, Clock::time_point now)
	{
		if (Toast* const toast = FindToast(m_Toasts, id); toast != nullptr && !toast->LeavingSince.has_value())
		{
			toast->LeavingSince = now;
			NoteDeparture(*toast, ToastDepartureReason::ClosedByUser);
		}
	}

	void ToastStack::Update(Clock::time_point now)
	{
		for (Toast& toast : m_Toasts)
		{
			const bool countdownStopped = toast.LeavingSince.has_value() || !toast.CountingFrom.has_value();
			if (countdownStopped)
			{
				continue;
			}

			// The fade starts when the countdown ran out, not at the frame that noticed: a frame late by a
			// long sleep must not stretch the toast's life.
			if (const Clock::time_point expiry = *toast.CountingFrom + toast.Left; now >= expiry)
			{
				toast.LeavingSince = expiry;
				NoteDeparture(toast, ToastDepartureReason::Expired);
			}
		}

		std::erase_if(m_Toasts, [now](const Toast& toast) { return HasFinishedLeaving(toast, now); });
	}

	std::optional<ToastStack::Clock::time_point> ToastStack::NextChange(Clock::time_point now) const
	{
		std::optional<Clock::time_point> earliestExpiry;

		for (const Toast& toast : m_Toasts)
		{
			const bool entering = now < toast.Shown + kEnter;

			if (entering || toast.LeavingSince.has_value())
			{
				return now;
			}

			if (toast.CountingFrom.has_value())
			{
				const Clock::time_point expiry = *toast.CountingFrom + toast.Left;
				earliestExpiry = earliestExpiry.has_value() ? std::min(*earliestExpiry, expiry) : expiry;
			}
		}

		return earliestExpiry;
	}

	bool ToastStack::IsAnyCountdownRunning() const
	{
		for (const Toast& toast : m_Toasts)
		{
			const bool countdownRunning = toast.CountingFrom.has_value() && !toast.LeavingSince.has_value();
			if (countdownRunning)
			{
				return true;
			}
		}

		return false;
	}

	ToastStack::Clock::duration ToastStack::LifetimeOf(ToastKind kind)
	{
		const bool isError = kind == ToastKind::Error;
		return isError ? kErrorLifetime : kWarningAndSuccessLifetime;
	}

	float ToastStack::ShareOfLifetimeLeft(const Toast& toast, Clock::time_point now)
	{
		Clock::duration timeLeft = toast.Left;
		if (toast.CountingFrom.has_value())
		{
			const Clock::time_point countedUntil =
				toast.LeavingSince.has_value() ? std::min(now, *toast.LeavingSince) : now;
			timeLeft -= countedUntil - *toast.CountingFrom;
		}

		const float shareLeft =
			std::chrono::duration<float>(timeLeft) / std::chrono::duration<float>(LifetimeOf(toast.Kind));
		return std::clamp(shareLeft, 0.0f, 1.0f);
	}
}

#include "corepch.h"

#include "Core/ImGui/ToastStack.h"

#include <algorithm>

namespace Core {

	namespace {
		bool Expires(ToastKind kind)
		{
			return kind != ToastKind::Error;
		}

		Toast* Find(std::vector<Toast>& toasts, std::uint64_t id)
		{
			const auto found = std::ranges::find(toasts, id, &Toast::Id);
			return found != toasts.end() ? &*found : nullptr;
		}
	}

	void ToastStack::Show(ToastKind kind, std::string title, std::string message, Clock::time_point now)
	{
		const auto twin = std::ranges::find_if(m_Toasts, [&](const Toast& toast) {
			return !toast.LeavingSince.has_value() && toast.Kind == kind && toast.Title == title &&
				   toast.Message == message;
		});

		if (twin != m_Toasts.end())
		{
			++twin->Count;
			twin->Left = kLifetime;

			// A hovered twin stays paused; the full lifetime starts when the pointer leaves.
			if (twin->CountingFrom.has_value())
			{
				twin->CountingFrom = now;
			}

			return;
		}

		Toast toast;
		toast.Id = m_NextId++;
		toast.Kind = kind;
		toast.Title = std::move(title);
		toast.Message = std::move(message);
		toast.Shown = now;
		toast.Left = kLifetime;
		toast.CountingFrom = now;

		m_Toasts.push_back(std::move(toast));
	}

	void ToastStack::SetHovered(std::uint64_t id, bool hovered, Clock::time_point now)
	{
		Toast* toast = Find(m_Toasts, id);

		if (toast == nullptr || toast->LeavingSince.has_value())
		{
			return;
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
		if (Toast* toast = Find(m_Toasts, id); toast != nullptr && !toast->LeavingSince.has_value())
		{
			toast->LeavingSince = now;
		}
	}

	void ToastStack::Update(Clock::time_point now)
	{
		for (Toast& toast : m_Toasts)
		{
			if (toast.LeavingSince.has_value() || !Expires(toast.Kind) || !toast.CountingFrom.has_value())
			{
				continue;
			}

			// The fade starts at the moment the countdown ran out, not at the frame that
			// noticed: a frame late by a long sleep must not stretch the toast's life.
			if (const Clock::time_point expiry = *toast.CountingFrom + toast.Left; now >= expiry)
			{
				toast.LeavingSince = expiry;
			}
		}

		std::erase_if(m_Toasts, [&](const Toast& toast) {
			return toast.LeavingSince.has_value() && now >= *toast.LeavingSince + kLeave;
		});
	}

	std::optional<ToastStack::Clock::time_point> ToastStack::NextChange(Clock::time_point now) const
	{
		std::optional<Clock::time_point> next;

		for (const Toast& toast : m_Toasts)
		{
			const bool entering = now < toast.Shown + kEnter;

			if (entering || toast.LeavingSince.has_value())
			{
				return now;
			}

			if (Expires(toast.Kind) && toast.CountingFrom.has_value())
			{
				const Clock::time_point expiry = *toast.CountingFrom + toast.Left;
				next = next.has_value() ? std::min(*next, expiry) : expiry;
			}
		}

		return next;
	}
}

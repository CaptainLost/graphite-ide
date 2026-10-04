#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	using ToastClock = std::chrono::steady_clock;

	enum class ToastKind
	{
		Success,
		Warning,
		Error
	};

	struct Toast
	{
		std::uint64_t Id = 0;
		ToastKind Kind = ToastKind::Error;

		// The action that failed or finished, drawn above the message in the bold face.
		std::string Title;
		std::string Message;

		// What Copy in the toast's menu copies; empty for the title and the message, one line
		// each. Set when the message on screen is shortened - paths written relative to the
		// repository - so the copy still carries the whole of what happened.
		std::string CopyText;

		// How many times this message was shown while the toast was up; drawn as "x2" past one.
		int Count = 1;

		// Start of the fade in.
		ToastClock::time_point Shown;

		// Set once the toast starts to fade; it is removed kLeave after this.
		std::optional<ToastClock::time_point> LeavingSince;

		// The countdown, for a kind that expires: the time left as of CountingFrom, and
		// CountingFrom empty while the pointer rests on the toast.
		ToastClock::duration Left{};
		std::optional<ToastClock::time_point> CountingFrom;
	};

	// What toasts are up and where each is in its life, kept apart from drawing so it can be
	// tested against a clock the test controls. Times are passed in rather than read: the
	// renderer passes the frame's now, a test passes whatever it wants.
	class ToastStack
	{
	public:
		using Clock = ToastClock;

		static constexpr Clock::duration kLifetime = std::chrono::seconds(6);
		static constexpr Clock::duration kEnter = std::chrono::milliseconds(150);
		static constexpr Clock::duration kLeave = std::chrono::milliseconds(150);

		// A message identical in kind, title and text to a toast still up, and not leaving, is
		// not a new toast: that one counts it and starts its countdown afresh. Two clicks on the
		// same failing action would otherwise stack two copies of the same words. The title
		// takes part because the same words from two different actions are two reports.
		void Show(ToastKind kind, std::string title, std::string message, Clock::time_point now,
				  std::string copyText = {});

		// Called every frame for every toast drawn; only a change of state does anything.
		void SetHovered(std::uint64_t id, bool hovered, Clock::time_point now);

		void Close(std::uint64_t id, Clock::time_point now);

		// Starts the fade of every toast whose countdown ran out, and drops every toast whose
		// fade has ended.
		void Update(Clock::time_point now);

		// Drops every toast at once, with no fade: what they reported belongs to something that
		// is gone, such as the folder a failed git write was made in.
		void Clear() { m_Toasts.clear(); }

		const std::vector<Toast>& Get() const { return m_Toasts; }

		// When the stack next changes on its own: now while anything animates, else the
		// earliest expiry, else nothing - an error waiting for its close button changes only
		// on input, which wakes the loop by itself.
		std::optional<Clock::time_point> NextChange(Clock::time_point now) const;

	private:
		std::vector<Toast> m_Toasts;
		std::uint64_t m_NextId = 1;
	};
}

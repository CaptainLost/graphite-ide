#pragma once

#include <cstdint>
#include <utility>

namespace Core {

	// Owns a Win32 kernel handle and closes it exactly once. Spelled as void*, which is what
	// HANDLE is, so a public header can hold one without pulling in <windows.h>. Both null and
	// INVALID_HANDLE_VALUE mean "nothing owned": the Win32 calls that hand handles out disagree
	// about which of the two they return on failure, and an owner that accepted either would
	// end up closing a value that was never a handle.
	class UniqueHandle
	{
	public:
		UniqueHandle() = default;
		explicit UniqueHandle(void* handle) noexcept : m_Handle(Normalise(handle)) {}

		UniqueHandle(const UniqueHandle&) = delete;
		UniqueHandle& operator=(const UniqueHandle&) = delete;

		UniqueHandle(UniqueHandle&& other) noexcept : m_Handle(other.Release()) {}

		UniqueHandle& operator=(UniqueHandle&& other) noexcept
		{
			if (this != &other)
			{
				Reset(other.Release());
			}

			return *this;
		}

		~UniqueHandle() { Reset(); }

		void* Get() const noexcept { return m_Handle; }
		explicit operator bool() const noexcept { return m_Handle != nullptr; }

		// Gives the handle up without closing it; the caller owns it from here.
		[[nodiscard]] void* Release() noexcept { return std::exchange(m_Handle, nullptr); }

		// Closes what is held, then takes ownership of the new handle. Closing overwrites the
		// thread's last error, so a caller about to report one must read it first.
		void Reset(void* handle = nullptr) noexcept;

	private:
		static void* Normalise(void* handle) noexcept
		{
			// INVALID_HANDLE_VALUE, written out so this header needs no <windows.h>.
			return handle == reinterpret_cast<void*>(static_cast<std::intptr_t>(-1)) ? nullptr : handle;
		}

		void* m_Handle = nullptr;
	};
}

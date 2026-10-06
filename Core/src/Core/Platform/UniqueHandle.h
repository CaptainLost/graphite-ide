#pragma once

#include <cstdint>
#include <utility>

namespace Core {

	// Holds void*, which is what HANDLE is, so a public header can own a handle without pulling in
	// <windows.h>.
	class UniqueHandle
	{
	public:
		UniqueHandle() = default;
		explicit UniqueHandle(void* handle) : m_Handle(NullIfInvalid(handle)) {}

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

		void* Get() const { return m_Handle; }
		explicit operator bool() const { return m_Handle != nullptr; }

		void* Release() { return std::exchange(m_Handle, nullptr); }

		// Closing overwrites the thread's last error, so a caller about to report one must read it
		// first.
		void Reset(void* replacement = nullptr);

	private:
		static void* InvalidHandleValue() { return reinterpret_cast<void*>(static_cast<std::intptr_t>(-1)); }

		// The Win32 calls that hand out handles disagree about whether null or INVALID_HANDLE_VALUE
		// reports a failure, so both mean that nothing is owned.
		static void* NullIfInvalid(void* handle)
		{
			const bool isInvalidHandleValue = handle == InvalidHandleValue();

			return isInvalidHandleValue ? nullptr : handle;
		}

		void* m_Handle = nullptr;
	};
}

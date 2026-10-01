#pragma once

// Private to Core's .cpp files: never include this from a public header, because it
// pulls in <windows.h>.

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

#include <utility>

namespace Core {

	// Owns a Win32 kernel handle and closes it exactly once. Both null and
	// INVALID_HANDLE_VALUE mean "nothing owned": the Win32 calls that hand handles out
	// disagree about which of the two they return on failure, and an owner that accepted
	// either would end up closing a value that was never a handle.
	class UniqueHandle
	{
	public:
		UniqueHandle() = default;
		explicit UniqueHandle(HANDLE handle) noexcept : m_Handle(Normalise(handle)) {}

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

		HANDLE Get() const noexcept { return m_Handle; }
		explicit operator bool() const noexcept { return m_Handle != nullptr; }

		// Gives the handle up without closing it; the caller owns it from here.
		[[nodiscard]] HANDLE Release() noexcept { return std::exchange(m_Handle, nullptr); }

		// Closes what is held, then takes ownership of the new handle. Closing overwrites the
		// thread's last error, so a caller about to report one must read it first.
		void Reset(HANDLE handle = nullptr) noexcept
		{
			const HANDLE old = std::exchange(m_Handle, Normalise(handle));

			if (old != nullptr)
			{
				::CloseHandle(old);
			}
		}

	private:
		static HANDLE Normalise(HANDLE handle) noexcept
		{
			return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
		}

		HANDLE m_Handle = nullptr;
	};

	// Owns an initialised PROC_THREAD_ATTRIBUTE_LIST. Initialised lists own more than the
	// block they were written into, so returning that block is not enough: the API contract
	// is to delete the list first, and only a list that was initialised may be deleted.
	class ProcThreadAttributeList
	{
	public:
		ProcThreadAttributeList() = default;

		ProcThreadAttributeList(const ProcThreadAttributeList&) = delete;
		ProcThreadAttributeList& operator=(const ProcThreadAttributeList&) = delete;

		ProcThreadAttributeList(ProcThreadAttributeList&& other) noexcept
			: m_List(other.Release())
		{
		}

		ProcThreadAttributeList& operator=(ProcThreadAttributeList&& other) noexcept
		{
			if (this != &other)
			{
				Destroy(std::exchange(m_List, other.Release()));
			}

			return *this;
		}

		~ProcThreadAttributeList() { Destroy(m_List); }

		// Allocates and initialises room for the given number of attributes. On failure the
		// thread's last error describes why and nothing is held.
		[[nodiscard]] bool Initialise(DWORD attributeCount)
		{
			Destroy(std::exchange(m_List, nullptr));

			SIZE_T bytes = 0;
			::InitializeProcThreadAttributeList(nullptr, attributeCount, 0, &bytes);

			auto* list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
				::HeapAlloc(::GetProcessHeap(), 0, bytes));

			if (list == nullptr)
			{
				::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
				return false;
			}

			if (!::InitializeProcThreadAttributeList(list, attributeCount, 0, &bytes))
			{
				const DWORD failure = ::GetLastError();
				::HeapFree(::GetProcessHeap(), 0, list);
				::SetLastError(failure);
				return false;
			}

			m_List = list;
			return true;
		}

		// The value must outlive every use of the list, as the underlying API requires.
		[[nodiscard]] bool Update(DWORD_PTR attribute, void* value, SIZE_T size)
		{
			return ::UpdateProcThreadAttribute(m_List, 0, attribute, value, size, nullptr,
											   nullptr) != FALSE;
		}

		LPPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept { return m_List; }

		// Gives the list up; the caller must pass it to Destroy exactly once.
		[[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST Release() noexcept
		{
			return std::exchange(m_List, nullptr);
		}

		// Deletes and frees a list that Initialise produced. Null is accepted.
		static void Destroy(LPPROC_THREAD_ATTRIBUTE_LIST list) noexcept
		{
			if (list != nullptr)
			{
				::DeleteProcThreadAttributeList(list);
				::HeapFree(::GetProcessHeap(), 0, list);
			}
		}

	private:
		LPPROC_THREAD_ATTRIBUTE_LIST m_List = nullptr;
	};

	// Owns a pseudo-console. Closing waits for the host's final flush, so whoever holds
	// one must keep the reading end of its output pipe open until this is closed.
	class UniquePseudoConsole
	{
	public:
		UniquePseudoConsole() = default;

		UniquePseudoConsole(const UniquePseudoConsole&) = delete;
		UniquePseudoConsole& operator=(const UniquePseudoConsole&) = delete;

		UniquePseudoConsole(UniquePseudoConsole&& other) noexcept : m_Console(other.Release()) {}

		UniquePseudoConsole& operator=(UniquePseudoConsole&& other) noexcept
		{
			if (this != &other)
			{
				Close(std::exchange(m_Console, other.Release()));
			}

			return *this;
		}

		~UniquePseudoConsole() { Close(m_Console); }

		// Receives the console CreatePseudoConsole produces; anything held before is closed.
		HPCON* Receive() noexcept
		{
			Close(std::exchange(m_Console, nullptr));
			return &m_Console;
		}

		HPCON Get() const noexcept { return m_Console; }
		[[nodiscard]] HPCON Release() noexcept { return std::exchange(m_Console, nullptr); }

	private:
		static void Close(HPCON console) noexcept
		{
			if (console != nullptr)
			{
				::ClosePseudoConsole(console);
			}
		}

		HPCON m_Console = nullptr;
	};
}

#endif

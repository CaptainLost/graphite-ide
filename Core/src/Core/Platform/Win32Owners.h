#pragma once

// Private to Core's .cpp files: never include this from a public header, because it
// pulls in <windows.h>.

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

#include <utility>

namespace Core {

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
		bool Initialise(DWORD attributeCount)
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
		bool Update(DWORD_PTR attribute, void* value, SIZE_T size)
		{
			return ::UpdateProcThreadAttribute(m_List, 0, attribute, value, size, nullptr,
											   nullptr) != FALSE;
		}

		LPPROC_THREAD_ATTRIBUTE_LIST Get() const { return m_List; }

		// Gives the list up; the caller must pass it to Destroy exactly once.
		LPPROC_THREAD_ATTRIBUTE_LIST Release()
		{
			return std::exchange(m_List, nullptr);
		}

		// Deletes and frees a list that Initialise produced. Null is accepted.
		static void Destroy(LPPROC_THREAD_ATTRIBUTE_LIST list)
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
		HPCON* Receive()
		{
			Close(std::exchange(m_Console, nullptr));
			return &m_Console;
		}

		HPCON Get() const { return m_Console; }
		HPCON Release() { return std::exchange(m_Console, nullptr); }

	private:
		static void Close(HPCON console)
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

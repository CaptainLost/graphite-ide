#pragma once

// Private to Core's .cpp files: it pulls in <windows.h>, so a public header must never include it.

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

#include <utility>

namespace Core {

	// An initialised list owns more than its memory block, so it is deleted before the block is
	// freed, and only a list that was initialised may be deleted.
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

		// On failure the thread's last error says why, so it is put back after HeapFree, which may
		// overwrite it.
		bool Initialise(DWORD attributeCount)
		{
			Destroy(std::exchange(m_List, nullptr));

			SIZE_T listBytes = 0;
			// The sizing call fails by design and only fills listBytes, so its result is not checked.
			::InitializeProcThreadAttributeList(kSizeQueryOnly, attributeCount, kNoFlags, &listBytes);

			auto* allocatedList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
				::HeapAlloc(::GetProcessHeap(), kNoFlags, listBytes));

			if (allocatedList == nullptr)
			{
				::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
				return false;
			}

			const BOOL initialised = ::InitializeProcThreadAttributeList(allocatedList, attributeCount, kNoFlags, &listBytes);

			if (!initialised)
			{
				const DWORD initialiseErrorCode = ::GetLastError();
				::HeapFree(::GetProcessHeap(), kNoFlags, allocatedList);
				::SetLastError(initialiseErrorCode);
				return false;
			}

			m_List = allocatedList;
			return true;
		}

		// The value must outlive every use of the list, as the underlying API requires.
		bool Update(DWORD_PTR attribute, void* value, SIZE_T valueSize)
		{
			return ::UpdateProcThreadAttribute(m_List, kNoFlags, attribute, value, valueSize, nullptr,
											   nullptr) != FALSE;
		}

		LPPROC_THREAD_ATTRIBUTE_LIST Get() const { return m_List; }

		// The caller must pass the list it is given to Destroy exactly once.
		LPPROC_THREAD_ATTRIBUTE_LIST Release()
		{
			return std::exchange(m_List, nullptr);
		}

		static void Destroy(LPPROC_THREAD_ATTRIBUTE_LIST list)
		{
			if (list != nullptr)
			{
				::DeleteProcThreadAttributeList(list);
				::HeapFree(::GetProcessHeap(), kNoFlags, list);
			}
		}

	private:
		static constexpr DWORD kNoFlags = 0;
		static constexpr LPPROC_THREAD_ATTRIBUTE_LIST kSizeQueryOnly = nullptr;

		LPPROC_THREAD_ATTRIBUTE_LIST m_List = nullptr;
	};

	// Closing waits for the host's final flush, so whoever holds one must keep the reading end of
	// its output pipe open until this is closed.
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

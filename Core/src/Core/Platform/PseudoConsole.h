#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/UniqueHandle.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	// A program that detects a pipe falls back to reduced behaviour, without colours, prompt or
	// interactivity, so the shell runs behind a pseudo-console that tells it it has a window. This
	// is not a flag on ChildProcess, which deliberately carries a framed protocol on a pipe.
	class PseudoConsole : NonCopyable
	{
	public:
		static Result<Scope<PseudoConsole>> Start(const std::filesystem::path& program,
												  const std::vector<std::string>& arguments,
												  const std::filesystem::path& workingDirectory,
												  std::uint16_t columns, std::uint16_t rows);

		~PseudoConsole();

		// Blocks until bytes arrive. Zero does not by itself mean the shell has exited, since an empty
		// buffer, a failed wait and a failed read return zero too: confirm with IsRunning() before
		// treating zero as the end.
		std::size_t Read(std::span<char> buffer);

		bool Write(std::string_view bytes);

		Result<void> Resize(std::uint16_t columns, std::uint16_t rows);

		// Releases a thread blocked in Read by killing the shell while keeping the handles that thread
		// waits on; destroying the object instead would pull them out from under it.
		void Terminate();

		bool IsRunning() const;

	private:
		PseudoConsole() = default;

		std::atomic<bool> m_TerminateRequested{false};

		// An HPCON and a PROC_THREAD_ATTRIBUTE_LIST, spelled void* to keep <windows.h> out of this
		// header. The destructor releases them by hand, in the order the platform needs.
		void* m_Console = nullptr;
		void* m_Attributes = nullptr;

		UniqueHandle m_Process;
		UniqueHandle m_ToChild;
		UniqueHandle m_FromChild;
	};
}

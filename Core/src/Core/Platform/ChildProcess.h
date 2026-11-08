#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/UniqueHandle.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct EnvironmentVariable
	{
		std::string Name;
		std::string Value;
	};

	struct ChildProcessOptions
	{
		// Without it the child's stderr goes where this process's own goes, or to the null device.
		bool PipeErrors = false;
	};

	class ChildProcess : NonCopyable
	{
	public:
		// The child inherits this process's environment, with `environment` set over it.
		static Result<Scope<ChildProcess>> Start(const std::filesystem::path& program,
												 const std::vector<std::string>& arguments,
												 const std::filesystem::path& workingDirectory,
												 std::span<const EnvironmentVariable> environment = {},
												 ChildProcessOptions options = {});

		// Terminates the child: one outliving this object would be writing into pipes that no longer
		// have a reader.
		~ChildProcess();

		std::size_t Read(std::span<char> buffer);

		// Reads stderr as Read reads stdout; valid only for a child started with PipeErrors. Each pipe has a buffer
		// of its own that the child blocks on once full, so a child writing much to both needs both read at once.
		std::size_t ReadErrors(std::span<char> buffer);

		bool Write(std::string_view bytes);

		// Safe while another thread is blocked in Read, which the child's death releases. It exists
		// apart from the destructor because it leaves this side's handles open, where destroying the
		// object would pull them out from beneath that thread.
		void Terminate();

		// True while any process of the child's job runs, not only the one Start launched.
		bool IsRunning() const;

		// Waits for the process Start launched alone: one it started may still be running when this returns true.
		bool WaitForExit(std::chrono::milliseconds timeout) const;

		// The code of the process Start launched, once it has ended; empty while it runs.
		std::optional<std::uint32_t> GetExitCode() const;

	private:
		ChildProcess() = default;

		UniqueHandle m_Job;
		UniqueHandle m_Process;
		UniqueHandle m_ToChild;
		UniqueHandle m_FromChild;
		UniqueHandle m_ErrorsFromChild;
	};
}

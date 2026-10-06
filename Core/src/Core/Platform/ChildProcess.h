#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/UniqueHandle.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class ChildProcess : NonCopyable
	{
	public:
		static Result<Scope<ChildProcess>> Start(const std::filesystem::path& program,
												 const std::vector<std::string>& arguments,
												 const std::filesystem::path& workingDirectory);

		// Terminates the child: one outliving this object would be writing into pipes that no longer
		// have a reader.
		~ChildProcess();

		std::size_t Read(std::span<char> buffer);

		bool Write(std::string_view bytes);

		// Safe while another thread is blocked in Read, which the child's death releases. It exists
		// apart from the destructor because it leaves this side's handles open, where destroying the
		// object would pull them out from beneath that thread.
		void Terminate();

		bool IsRunning() const;

	private:
		ChildProcess() = default;

		UniqueHandle m_Process;
		UniqueHandle m_ToChild;
		UniqueHandle m_FromChild;
	};
}

#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	// A running program with its standard input and output redirected to this process. Nothing
	// here knows why it was started: this is a system primitive of the same kind as FileDialog
	// and Shell, which is what lets it sit in Core under ADR 0002.
	class ChildProcess : NonCopyable
	{
	public:
		static Result<Scope<ChildProcess>> Start(const std::filesystem::path& program,
												 const std::vector<std::string>& arguments,
												 const std::filesystem::path& workingDirectory);

		// Terminates the child and closes both pipes. A child outliving this object would be
		// writing into handles that no longer have a reader.
		~ChildProcess();

		// Blocks until at least one byte arrives or the pipe closes. Zero means closed, which is
		// how a caller learns the child is gone without polling IsRunning in a loop.
		std::size_t Read(std::span<char> buffer);

		bool Write(std::string_view bytes);

		bool IsRunning() const;

	private:
		ChildProcess() = default;

		void* m_Process = nullptr;
		void* m_ToChild = nullptr;
		void* m_FromChild = nullptr;
	};
}

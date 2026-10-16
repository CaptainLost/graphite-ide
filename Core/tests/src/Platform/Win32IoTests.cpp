#include <doctest/doctest.h>

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Io.h"

#include <array>
#include <cstddef>
#include <string>
#include <thread>

namespace {
	constexpr DWORD kSmallPipeBufferBytes = 512;
	constexpr std::size_t kManyPipeBuffers = 64 * 1024;
	constexpr int kLettersInTheAlphabet = 26;

	struct Pipe
	{
		Core::UniqueHandle ReadEnd;
		Core::UniqueHandle WriteEnd;
	};

	Pipe MakePipe()
	{
		HANDLE readEnd = nullptr;
		HANDLE writeEnd = nullptr;
		REQUIRE(::CreatePipe(&readEnd, &writeEnd, nullptr, kSmallPipeBufferBytes));

		return Pipe{.ReadEnd = Core::UniqueHandle{readEnd}, .WriteEnd = Core::UniqueHandle{writeEnd}};
	}

	std::string ReadUntilClosed(HANDLE readEnd)
	{
		std::string received;
		std::array<char, kSmallPipeBufferBytes> chunk{};
		DWORD bytesRead = 0;

		while (::ReadFile(readEnd, chunk.data(), static_cast<DWORD>(chunk.size()), &bytesRead, nullptr) && bytesRead > 0)
		{
			received.append(chunk.data(), bytesRead);
		}

		return received;
	}
}

TEST_CASE("WriteAll delivers every byte of a write many times the size of the pipe's buffer")
{
	Pipe pipe = MakePipe();

	std::string sent;
	for (std::size_t index = 0; index < kManyPipeBuffers; ++index)
	{
		sent.push_back(static_cast<char>('a' + static_cast<int>(index) % kLettersInTheAlphabet));
	}

	std::string received;
	std::thread reader([&received, &pipe] { received = ReadUntilClosed(pipe.ReadEnd.Get()); });

	const bool written = Core::WriteAll(pipe.WriteEnd.Get(), sent);
	pipe.WriteEnd.Reset();
	reader.join();

	CHECK(written);
	CHECK(received == sent);
}

TEST_CASE("WriteAll reports a write that nobody can read any more as failed")
{
	Pipe pipe = MakePipe();
	pipe.ReadEnd.Reset();

	CHECK_FALSE(Core::WriteAll(pipe.WriteEnd.Get(), "lost"));
}

TEST_CASE("CancelAndCollect leaves no read outstanding")
{
	// Declared before the handles, so that whatever the kernel still writes as they close lands in a live frame.
	std::array<char, kSmallPipeBufferBytes> buffer{};
	OVERLAPPED overlapped{};

	// CreatePipe makes no overlapped end, so the read waits on a named pipe, as the pseudo-console's does.
	const std::wstring name = L"\\\\.\\pipe\\CoreTests.Win32Io." + std::to_wstring(::GetCurrentProcessId());
	const DWORD onlyInstance = 1;
	const DWORD systemDefaultTimeout = 0;

	const Core::UniqueHandle readEnd{::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
														PIPE_TYPE_BYTE | PIPE_WAIT, onlyInstance, kSmallPipeBufferBytes,
														kSmallPipeBufferBytes, systemDefaultTimeout, nullptr)};
	REQUIRE(readEnd);

	const DWORD shareWithNoOne = 0;
	const Core::UniqueHandle writeEnd{
		::CreateFileW(name.c_str(), GENERIC_WRITE, shareWithNoOne, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
	REQUIRE(writeEnd);

	const BOOL completedAtOnce =
		::ReadFile(readEnd.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, &overlapped);
	REQUIRE_FALSE(completedAtOnce);
	REQUIRE(::GetLastError() == ERROR_IO_PENDING);

	Core::CancelAndCollect(readEnd.Get(), overlapped);

	CHECK(HasOverlappedIoCompleted(&overlapped));
}

#endif

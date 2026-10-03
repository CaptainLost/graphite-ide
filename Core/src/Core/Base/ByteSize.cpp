#include "corepch.h"

#include "Core/Base/ByteSize.h"

#include <format>

namespace Core {

	namespace {
		constexpr std::uint64_t kKilobyte = 1024;
		constexpr std::uint64_t kMegabyte = 1024 * kKilobyte;
	}

	std::string FormatByteSize(std::uint64_t bytes)
	{
		if (bytes < kKilobyte)
		{
			return std::format("{} B", bytes);
		}

		if (bytes < kMegabyte)
		{
			return std::format("{:.1f} KB", static_cast<double>(bytes) / static_cast<double>(kKilobyte));
		}

		return std::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(kMegabyte));
	}

	std::string FormatWholeKilobytes(std::uint64_t bytes)
	{
		return std::format("{} KB", bytes / kKilobyte);
	}

	std::string FormatWholeMegabytes(std::uint64_t bytes)
	{
		return std::format("{} MB", bytes / kMegabyte);
	}
}

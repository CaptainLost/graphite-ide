#include "corepch.h"

#include "Core/Base/ByteSize.h"

#include <format>

namespace Core {

	namespace {
		constexpr std::uint64_t kKilobyte = 1024;
		constexpr std::uint64_t kMegabyte = 1024 * kKilobyte;
		constexpr std::uint64_t kGigabyte = 1024 * kMegabyte;
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

		if (bytes < kGigabyte)
		{
			return std::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(kMegabyte));
		}

		return std::format("{:.1f} GB", static_cast<double>(bytes) / static_cast<double>(kGigabyte));
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

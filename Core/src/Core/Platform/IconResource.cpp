#include "corepch.h"

#include "Core/Platform/IconResource.h"

#include "Core/Base/ScopeExit.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace Core {

	RgbaImage LoadIconResource([[maybe_unused]] std::string_view name, [[maybe_unused]] int size)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (size <= 0)
		{
			return {};
		}

		// Resource names in a .rc script are ASCII, so the narrow call reads them as written.
		const std::string resource(name);
		const auto icon = static_cast<HICON>(
			::LoadImageA(::GetModuleHandleW(nullptr), resource.c_str(), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));

		if (icon == nullptr)
		{
			return {};
		}

		const ScopeExit destroyIcon([icon] { ::DestroyIcon(icon); });

		ICONINFO info{};

		if (!::GetIconInfo(icon, &info))
		{
			return {};
		}

		// Both bitmaps are copies the caller owns, whichever of them is used.
		const ScopeExit deleteBitmaps([&info] {
			::DeleteObject(info.hbmColor);
			::DeleteObject(info.hbmMask);
		});

		if (info.hbmColor == nullptr)
		{
			return {};
		}

		BITMAPINFO format{};
		format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		format.bmiHeader.biWidth = size;
		// Negative for rows top to bottom, the order RgbaImage keeps them in.
		format.bmiHeader.biHeight = -size;
		format.bmiHeader.biPlanes = 1;
		format.bmiHeader.biBitCount = 32;
		format.bmiHeader.biCompression = BI_RGB;

		RgbaImage image;
		image.Width = size;
		image.Height = size;
		image.Pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);

		const HDC screen = ::GetDC(nullptr);
		const int rows = ::GetDIBits(screen, info.hbmColor, 0, static_cast<UINT>(size), image.Pixels.data(), &format,
									 DIB_RGB_COLORS);
		::ReleaseDC(nullptr, screen);

		if (rows != size)
		{
			return {};
		}

		// GDI hands the bytes over as blue, green, red, alpha.
		for (std::size_t i = 0; i < image.Pixels.size(); i += 4)
		{
			std::swap(image.Pixels[i], image.Pixels[i + 2]);
		}

		return image;
#else
		return {};
#endif
	}

	std::vector<int> IconResourceSizes([[maybe_unused]] std::string_view name)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::string resource(name);
		const HMODULE module = ::GetModuleHandleW(nullptr);
		const HRSRC found = ::FindResourceA(module, resource.c_str(), MAKEINTRESOURCEA(14)); // RT_GROUP_ICON

		if (found == nullptr)
		{
			return {};
		}

		const HGLOBAL loaded = ::LoadResource(module, found);
		const auto* bytes = loaded != nullptr ? static_cast<const std::uint8_t*>(::LockResource(loaded)) : nullptr;
		const DWORD length = ::SizeofResource(module, found);

		// The group's directory: a six-byte header whose last two bytes count the entries,
		// then fourteen bytes an entry, the first byte of each its width, with 0 for 256.
		constexpr std::size_t kHeader = 6;
		constexpr std::size_t kEntry = 14;

		if (bytes == nullptr || length < kHeader)
		{
			return {};
		}

		const std::size_t count = static_cast<std::size_t>(bytes[4]) | (static_cast<std::size_t>(bytes[5]) << 8);

		if (length < kHeader + count * kEntry)
		{
			return {};
		}

		std::vector<int> sizes;

		for (std::size_t i = 0; i < count; ++i)
		{
			const int width = bytes[kHeader + i * kEntry];
			sizes.push_back(width == 0 ? 256 : width);
		}

		std::sort(sizes.begin(), sizes.end());
		sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
		return sizes;
#else
		return {};
#endif
	}
}

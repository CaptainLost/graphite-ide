#include "corepch.h"

#include "Core/FileSystem/WslSharePath.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <array>
#include <string_view>

namespace Core {

	namespace {

		constexpr std::string_view kShareRoot = "//";
		constexpr std::string_view kLongUncPrefix = "//?/UNC/";
		constexpr std::array<std::string_view, 2> kWslHosts{"wsl.localhost", "wsl$"};

		bool IsWslHost(std::string_view host)
		{
			for (const std::string_view wslHost : kWslHosts)
			{
				if (EqualsIgnoringCase(host, wslHost))
				{
					return true;
				}
			}

			return false;
		}

		// What WSL's share spells as U+F000 plus the character: what Windows forbids in a name, but '/'.
		constexpr std::string_view kMappedByTheShare = "\\:*?\"<>|";
		constexpr char32_t kShareMappingBase = 0xF000;

		void AppendShareSpelling(std::string& text, char character)
		{
			if (kMappedByTheShare.find(character) == std::string_view::npos)
			{
				text.push_back(character);
				return;
			}

			// U+F000 plus an ASCII character, in UTF-8: three bytes, EF 80..81 80..BF.
			const char32_t mapped = kShareMappingBase + static_cast<unsigned char>(character);
			text.push_back(static_cast<char>(0xE0 | (mapped >> 12)));
			text.push_back(static_cast<char>(0x80 | ((mapped >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (mapped & 0x3F)));
		}

		// The share's spelling read back: each mapped character to the one it stands for.
		std::string FromShareSpelling(std::string_view text)
		{
			std::string linuxPath;
			linuxPath.reserve(text.size());

			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };

				if (index + 2 < text.size() && byte(index) == 0xEF && (byte(index + 1) & 0xFE) == 0x80 && (byte(index + 2) & 0xC0) == 0x80)
				{
					const unsigned mapped = ((byte(index + 1) & 0x3Fu) << 6) | (byte(index + 2) & 0x3Fu);

					if (mapped < 0x80 && kMappedByTheShare.find(static_cast<char>(mapped)) != std::string_view::npos)
					{
						linuxPath.push_back(static_cast<char>(mapped));
						index += 2;
						continue;
					}
				}

				linuxPath.push_back(text[index]);
			}

			return linuxPath;
		}

		// Cuts the text up to the next '/', and the '/' itself.
		std::string_view TakeSegment(std::string_view& rest)
		{
			const std::size_t separator = rest.find('/');
			const std::string_view segment = rest.substr(0, separator);

			rest = separator == std::string_view::npos ? std::string_view{} : rest.substr(separator + 1);

			return segment;
		}
	}

	std::optional<WslSharePath> ParseWslSharePath(const std::filesystem::path& path)
	{
		std::string text = ToUtf8(path);

		for (char& character : text)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		std::string_view rest = text;

		if (rest.starts_with(kLongUncPrefix))
		{
			rest.remove_prefix(kLongUncPrefix.size());
		}
		else if (rest.starts_with(kShareRoot))
		{
			rest.remove_prefix(kShareRoot.size());
		}
		else
		{
			return std::nullopt;
		}

		const std::string_view host = TakeSegment(rest);
		const std::string_view distro = TakeSegment(rest);

		if (!IsWslHost(host) || distro.empty())
		{
			return std::nullopt;
		}

		while (rest.ends_with('/'))
		{
			rest.remove_suffix(1);
		}

		return WslSharePath{std::string{distro}, "/" + FromShareSpelling(rest)};
	}

	std::filesystem::path ToWslSharePath(const WslSharePath& place)
	{
		std::string share = "\\\\wsl.localhost\\" + place.Distro;

		for (const char character : place.LinuxPath)
		{
			if (character == '/')
			{
				share.push_back('\\');
				continue;
			}

			AppendShareSpelling(share, character);
		}

		return FromUtf8(share);
	}

	std::string ToWslShareSpelling(std::string_view linuxPath)
	{
		std::string share;
		share.reserve(linuxPath.size());

		for (const char character : linuxPath)
		{
			AppendShareSpelling(share, character);
		}

		return share;
	}

	std::filesystem::path CanonicalWslSharePath(const std::filesystem::path& path)
	{
		const std::optional<WslSharePath> place = ParseWslSharePath(path);
		return place ? ToWslSharePath(*place) : path;
	}
}

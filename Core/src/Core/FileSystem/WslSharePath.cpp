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

		return WslSharePath{std::string{distro}, "/" + std::string{rest}};
	}
}

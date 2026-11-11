#include "Wsl/WslServerUris.h"

#include "Language/Server/LspProtocol.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace Graphite {

	namespace {

		constexpr std::string_view kFileScheme = "file:";
		constexpr std::string_view kRootPathKey = "rootPath";

		using StringMap = std::function<std::optional<std::string>(const std::string&)>;

		// Rewrites each string the map answers for, everywhere in the value; true when any changed.
		bool Rewrite(nlohmann::json& value, const StringMap& map)
		{
			bool changed = false;

			if (value.is_string())
			{
				if (std::optional<std::string> mapped = map(value.get_ref<const std::string&>()))
				{
					value = std::move(*mapped);
					changed = true;
				}
			}
			else if (value.is_structured())
			{
				for (nlohmann::json& element : value)
				{
					changed = Rewrite(element, map) || changed;
				}
			}

			return changed;
		}

		constexpr std::string_view kLinuxFileUriPrefix = "file://";

		bool IsAsciiLetter(char character)
		{
			return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
		}

		bool IsAsciiLetterOrDigit(char character)
		{
			return IsAsciiLetter(character) || (character >= '0' && character <= '9');
		}

		// A file URI is one word: a string with whitespace or a control character in it is text
		// (a document's, a message's) that happens to start with "file:".
		bool LooksLikeFileUri(const std::string& text)
		{
			if (!text.starts_with(kFileScheme))
			{
				return false;
			}

			for (const char character : text)
			{
				if (static_cast<unsigned char>(character) <= ' ' || character == '\x7F')
				{
					return false;
				}
			}

			return true;
		}

		// As Graphite's own URIs spell a path (ToFileUri), but byte for byte: a Linux name may hold
		// a '\', which a std::filesystem::path on Windows would take for a separator.
		std::string LinuxFileUri(std::string_view linuxPath)
		{
			constexpr std::string_view kHexDigits = "0123456789ABCDEF";
			std::string uri{kLinuxFileUriPrefix};

			for (const char character : linuxPath)
			{
				const unsigned char byte = static_cast<unsigned char>(character);

				if (IsAsciiLetterOrDigit(character) || std::string_view{"-._~/:"}.find(character) != std::string_view::npos)
				{
					uri.push_back(character);
					continue;
				}

				uri.push_back('%');
				uri.push_back(kHexDigits[byte >> 4]);
				uri.push_back(kHexDigits[byte & 0x0F]);
			}

			return uri;
		}

		// The Linux path a file:///... URI names, decoded byte for byte; none for any other URI.
		std::optional<std::string> LinuxPathOf(std::string_view uri)
		{
			if (!uri.starts_with(kLinuxFileUriPrefix))
			{
				return std::nullopt;
			}

			uri.remove_prefix(kLinuxFileUriPrefix.size());

			// file:///path only: file://host/... and file:////share/... are not the distro's.
			if (!uri.starts_with('/') || uri.starts_with("//"))
			{
				return std::nullopt;
			}

			std::string path;
			path.reserve(uri.size());

			for (std::size_t index = 0; index < uri.size(); ++index)
			{
				if (uri[index] == '%' && index + 2 < uri.size())
				{
					const std::optional<std::uint8_t> high = Core::HexDigitValue(uri[index + 1]);
					const std::optional<std::uint8_t> low = Core::HexDigitValue(uri[index + 2]);

					if (high && low)
					{
						path.push_back(static_cast<char>((*high << 4) | *low));
						index += 2;
						continue;
					}
				}

				path.push_back(uri[index]);
			}

			// A drive (file:///C:/...) is Windows', whatever a Linux server says.
			if (path.size() >= 3 && path[2] == ':' && IsAsciiLetter(path[1]) && (path.size() == 3 || path[3] == '/'))
			{
				return std::nullopt;
			}

			return path;
		}

		std::optional<std::string> ToDistroUri(const std::string& text, const Core::WslSharePath& root)
		{
			if (!LooksLikeFileUri(text))
			{
				return std::nullopt;
			}

			const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(FromFileUri(text));

			// ADR 0696: a distro's name is one in any case.
			if (!place || !Core::EqualsIgnoringCase(place->Distro, root.Distro))
			{
				return std::nullopt;
			}

			return LinuxFileUri(place->LinuxPath);
		}

		std::optional<std::string> FromDistroUri(const std::string& text, const std::string& distro)
		{
			if (!LooksLikeFileUri(text))
			{
				return std::nullopt;
			}

			const std::optional<std::string> linuxPath = LinuxPathOf(text);

			if (!linuxPath)
			{
				return std::nullopt;
			}

			return ToFileUri(Core::ToWslSharePath(Core::WslSharePath{distro, *linuxPath}));
		}

		std::string Translated(std::string_view body, bool mayHoldRootPath, const StringMap& map)
		{
			// Most bodies (semantic tokens, hover, progress) hold no file URI: no parse for them.
			if (body.find(kFileScheme) == std::string_view::npos && !(mayHoldRootPath && body.find(kRootPathKey) != std::string_view::npos))
			{
				return std::string{body};
			}

			nlohmann::json message = nlohmann::json::parse(body, nullptr, false);

			if (message.is_discarded())
			{
				return std::string{body};
			}

			bool changed = Rewrite(message, map);

			if (mayHoldRootPath)
			{
				const auto params = message.find("params");

				if (params != message.end() && params->is_object())
				{
					const auto rootPath = params->find(kRootPathKey);

					if (rootPath != params->end() && rootPath->is_string())
					{
						if (const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(Core::FromUtf8(rootPath->get<std::string>())))
						{
							*rootPath = place->LinuxPath;
							changed = true;
						}
					}
				}
			}

			return changed ? message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : std::string{body};
		}
	}

	std::string ToDistroBody(std::string_view body, const Core::WslSharePath& root)
	{
		return Translated(body, true, [&root](const std::string& text) { return ToDistroUri(text, root); });
	}

	std::string FromDistroBody(std::string_view body, const std::string& distro)
	{
		return Translated(body, false, [&distro](const std::string& text) { return FromDistroUri(text, distro); });
	}
}

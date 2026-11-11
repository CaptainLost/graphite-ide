#include "Wsl/WslServerUris.h"

#include "Language/Server/LspProtocol.h"

#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

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

		std::optional<std::string> ToDistroUri(const std::string& text, const Core::WslSharePath& root)
		{
			if (!text.starts_with(kFileScheme))
			{
				return std::nullopt;
			}

			const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(FromFileUri(text));

			if (!place || place->Distro != root.Distro)
			{
				return std::nullopt;
			}

			return ToFileUri(Core::FromUtf8(place->LinuxPath));
		}

		std::optional<std::string> FromDistroUri(const std::string& text, const std::string& distro)
		{
			if (!text.starts_with(kFileScheme))
			{
				return std::nullopt;
			}

			const std::string linuxPath = Core::ToGenericUtf8(FromFileUri(text));

			// Only a Linux absolute path: a drive path or a share path is not the distro's.
			if (!linuxPath.starts_with('/') || linuxPath.starts_with("//"))
			{
				return std::nullopt;
			}

			return ToFileUri(Core::ToWslSharePath(Core::WslSharePath{distro, linuxPath}));
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

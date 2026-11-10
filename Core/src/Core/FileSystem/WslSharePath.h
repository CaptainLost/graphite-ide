#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	// A place inside a WSL distro, reached from Windows through its share.
	struct WslSharePath
	{
		// UTF-8, verbatim from the path, e.g. "Ubuntu-22.04".
		std::string Distro;

		// UTF-8, absolute, '/'-separated, "/" for the distro's root, no trailing '/'.
		std::string LinuxPath;

		bool operator==(const WslSharePath&) const = default;
	};

	// Recognises \\wsl.localhost\<distro>\... and \\wsl$\<distro>\..., in any case, with either
	// separator and with or without the \\?\UNC\ prefix. Anything else, including a share host
	// with no distro after it, is not a place inside WSL.
	std::optional<WslSharePath> ParseWslSharePath(const std::filesystem::path& path);

	// \\wsl.localhost\<Distro><LinuxPath with '\'>: the one spelling the host uses for a place in
	// WSL (ADR 0696), whatever spelling it arrived in.
	std::filesystem::path ToWslSharePath(const WslSharePath& place);

	// ToWslSharePath of any spelling ParseWslSharePath accepts; any other path unchanged.
	std::filesystem::path CanonicalWslSharePath(const std::filesystem::path& path);
}

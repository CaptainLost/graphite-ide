#include "Lang/Server/ServerRegistry.h"

#include <array>
#include <system_error>

namespace Graphite {

	namespace {

		// Order here does not affect RootFor's answer — it does not prefer one marker over
		// another, see the comment on RootFor in the header. ".git" trails the list on purpose: it names a
		// checkout, not a build, so a nearer compile_commands.json or .clangd should still win
		// when both exist above the same file — which RootFor's proximity-first walk already
		// guarantees without this ordering doing any work. It is kept last anyway because it
		// documents the reasoning for a reader who has not seen RootFor yet.
		constexpr std::array<std::string_view, 4> kCppRootMarkers{
			"compile_commands.json", "compile_flags.txt", ".clangd", ".git"};

		constexpr ServerConfig kCpp{"clangd", "cpp", kCppRootMarkers};

		constexpr std::array<std::string_view, 3> kCompileDatabaseNames{
			"compile_commands.json", "compile_flags.txt", ".clangd"};

		// The error_code overload, never the throwing one. Both walks below climb to the drive
		// root, and a directory that denies access — or a path that is not one at all — makes the
		// throwing overload raise a filesystem_error across a boundary ADR 0004 says nothing
		// throws across. "Cannot tell" reads as "not there", which is what a marker that cannot be
		// examined means for a walk looking for one.
		bool Exists(const std::filesystem::path& path)
		{
			std::error_code failed;
			return std::filesystem::exists(path, failed) && !failed;
		}
	}

	std::span<const LanguageId> LanguagesWithServers()
	{
		// Listed rather than derived by asking ServerConfigFor about every enumerator: the two go
		// out of step only if someone adds a row here and not there, which is one grep away, and
		// deriving it would mean iterating an enum that has no iteration order of its own.
		static constexpr std::array<LanguageId, 1> kConfigured{LanguageId::Cpp};

		return kConfigured;
	}

	const ServerConfig* ServerConfigFor(LanguageId language)
	{
		switch (language)
		{
			case LanguageId::Cpp:
				return &kCpp;
			case LanguageId::PlainText:
			case LanguageId::CSharp:
			case LanguageId::TypeScript:
				return nullptr;
		}

		return nullptr;
	}

	std::filesystem::path RootFor(const ServerConfig& config, const std::filesystem::path& file,
								   const std::filesystem::path& fallbackRoot)
	{
		std::filesystem::path directory = file.parent_path();

		for (;;)
		{
			for (const std::string_view marker : config.RootMarkers)
			{
				if (Exists(directory / marker))
				{
					return directory;
				}
			}

			const std::filesystem::path parent = directory.parent_path();
			if (parent == directory)
			{
				// Reached the filesystem root without finding a marker.
				break;
			}

			directory = parent;
		}

		return fallbackRoot;
	}

	bool LacksCompileDatabase(const std::filesystem::path& root)
	{
		for (const std::string_view name : kCompileDatabaseNames)
		{
			if (Exists(root / name))
			{
				return false;
			}
		}

		// compile_commands.json is also checked in build/, because that is where CMake puts it
		// by convention and clangd itself looks there — a project laid out that way is fully
		// configured, not lacking. compile_flags.txt and .clangd are not given the same
		// treatment: both are hand-authored specifically to sit beside the sources they describe
		// (compile_flags.txt has no notion of a subdirectory's worth of flags, and .clangd is
		// read relative to the file it configures), so neither convention places them in build/.
		if (Exists(root / "build" / "compile_commands.json"))
		{
			return false;
		}

		return true;
	}
}

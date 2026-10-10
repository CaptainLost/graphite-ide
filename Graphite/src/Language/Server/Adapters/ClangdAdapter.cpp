#include "Language/Server/Adapters/ClangdAdapter.h"

#include "Language/Server/ServerCatalog.h"

#include <array>
#include <span>
#include <string_view>
#include <system_error>

namespace Graphite {

	namespace {

		constexpr std::array<std::string_view, 3> kCompileDatabaseNames{"compile_commands.json", "compile_flags.txt", ".clangd"};

		// The error_code overload: a directory that denies access reads as "not there" instead of throwing across the boundary ADR 0004 guards.
		bool Exists(const std::filesystem::path& path)
		{
			std::error_code failure;
			return std::filesystem::exists(path, failure) && !failure;
		}
	}

	bool LacksCompileDatabase(const std::filesystem::path& root)
	{
		// compile_flags.txt and .clangd describe the sources beside them, so only compile_commands.json is also looked for in build/.
		const bool hasDatabaseBesideSources = DirectoryHoldsAnyMarker(root, kCompileDatabaseNames);
		const bool hasDatabaseInBuild = Exists(root / "build" / "compile_commands.json");

		return !hasDatabaseBesideSources && !hasDatabaseInBuild;
	}

	nlohmann::json ClangdAdapter::WorkspaceConfiguration(const AdapterContext&, std::string_view) const
	{
		// clangd holds its progress reporting until every item is answered, and an empty object is the answer it takes.
		return nlohmann::json::object();
	}

	std::optional<ProjectProblem> ClangdAdapter::ProjectProblemAt(const AdapterContext& context) const
	{
		if (!LacksCompileDatabase(context.Root))
		{
			return std::nullopt;
		}

		return ProjectProblem{.Summary = "no compile database",
							  .Explanation = "No compile_commands.json, compile_flags.txt or .clangd was found for this project, so "
											 "clangd can see only the files that are open rather than the whole project."};
	}

	bool ClangdAdapter::WarmUpNeedsOpen() const
	{
		// clangd loads its compile database, and starts indexing, only on the first document it is told about.
		return true;
	}

	bool ClangdAdapter::SupportsSwitchSourceHeader() const
	{
		return true;
	}

	bool ClangdAdapter::AnswersEmptyConfigurationWithOneEntry() const
	{
		// A reply with no entry stalls clangd as surely as no reply at all.
		return true;
	}
}

#include "Language/Server/Adapters/ClangdAdapter.h"

#include "Language/Server/ClangdHover.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/FileSystem/FileSystem.h"

#include <array>
#include <span>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::array<std::string_view, 3> kCompileDatabaseNames{"compile_commands.json", "compile_flags.txt", ".clangd"};
	}

	bool LacksCompileDatabase(const std::filesystem::path& root)
	{
		const bool hasDatabaseBesideSources = DirectoryHoldsAnyMarker(root, kCompileDatabaseNames);
		const bool hasDatabaseInBuild = Core::PathExists(root / "build" / "compile_commands.json");

		return !hasDatabaseBesideSources && !hasDatabaseInBuild;
	}

	nlohmann::json ClangdAdapter::WorkspaceConfiguration(const AdapterContext&, std::string_view) const
	{
		// WARNING: clangd holds its progress reporting until every item is answered, and an empty object is the answer it takes.
		return nlohmann::json::object();
	}

	HoverInfo ClangdAdapter::ParseHover(std::string_view markdown) const
	{
		return ParseClangdHover(markdown);
	}

	std::optional<ProjectProblem> ClangdAdapter::ProjectProblemAt(const AdapterContext& context) const
	{
		if (!LacksCompileDatabase(context.Root))
		{
			// WARNING: compile_flags.txt gives clangd the flags but no list of files, so it indexes nothing in the
			// background: references, and the counts above declarations, reach only the files open with it. A .clangd may
			// point clangd at a compile_commands.json elsewhere, so it is taken at its word.
			const bool hasCompileCommands = Core::PathExists(context.Root / "compile_commands.json") ||
											Core::PathExists(context.Root / "build" / "compile_commands.json");
			const bool flagsOnly = !hasCompileCommands && !Core::PathExists(context.Root / ".clangd") &&
								   Core::PathExists(context.Root / "compile_flags.txt");

			if (!flagsOnly)
			{
				return std::nullopt;
			}

			return ProjectProblem{.Summary = "open files only",
								  .Explanation = "compile_flags.txt gives clangd the flags but not the project's files, so "
												 "references and their counts reach only the files that are open. A "
												 "compile_commands.json lets clangd index the whole project."};
		}

		return ProjectProblem{.Summary = "no compile database",
							  .Explanation = "No compile_commands.json, compile_flags.txt or .clangd was found for this project, so "
											 "clangd can see only the files that are open rather than the whole project."};
	}

	bool ClangdAdapter::WarmUpNeedsOpen() const
	{
		// WARNING: clangd loads its compile database, and starts indexing, only on the first document it is told about.
		return true;
	}

	bool ClangdAdapter::SupportsSwitchSourceHeader() const
	{
		return true;
	}

	bool ClangdAdapter::AnswersEmptyConfigurationWithOneEntry() const
	{
		// WARNING: A reply with no entry stalls clangd as surely as no reply at all.
		return true;
	}
}

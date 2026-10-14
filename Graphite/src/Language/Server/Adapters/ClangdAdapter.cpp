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
			return std::nullopt;
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

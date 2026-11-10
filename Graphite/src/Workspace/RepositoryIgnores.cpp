#include "Workspace/RepositoryIgnores.h"

#include "Core/Git/Repository.h"

#include <memory>
#include <utility>

namespace Graphite {

	namespace {

		class LocalRepositoryIgnores final : public RepositoryIgnores
		{
		public:
			explicit LocalRepositoryIgnores(Core::Repository repository) : m_Repository(std::move(repository)) {}

			std::filesystem::path GetWorkingDirectory() const override { return m_Repository.GetWorkingDirectory(); }

			bool IsIgnored(const std::filesystem::path& path, [[maybe_unused]] bool isDirectory) override { return m_Repository.IsIgnored(path); }

			std::vector<std::filesystem::path> RuleFilesOutside(const std::filesystem::path& folder) override
			{
				return m_Repository.IgnoreRuleFilesOutside(folder);
			}

		private:
			Core::Repository m_Repository;
		};
	}

	Core::Result<Core::Scope<RepositoryIgnores>> OpenLocalRepositoryIgnores(const std::filesystem::path& root)
	{
		Core::Result<Core::Repository> repository = Core::Repository::Open(root);

		if (!repository)
		{
			return repository.GetError();
		}

		return Core::Scope<RepositoryIgnores>{std::make_unique<LocalRepositoryIgnores>(std::move(repository).GetValue())};
	}
}

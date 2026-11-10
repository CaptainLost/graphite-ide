#pragma once

#include "Wsl/WslGit.h"
#include "Workspace/RepositoryIgnores.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslSharePath.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	class WslAgents;

	// The Explorer's ignore questions about a folder inside WSL, answered by the distro's agent on
	// its quick lane (ADR 0698): libgit2 never opens the repository over the share. The first
	// question about an entry of a folder lists the folder and asks about all its entries at once;
	// the answers are kept until ForgetAnswers, and a name they do not list asks the folder again.
	class WslIgnores final : public RepositoryIgnores
	{
	public:
		using Lister = std::function<Core::Result<std::vector<Core::DirectoryEntry>>(const std::filesystem::path& folder)>;

		// Cancelled while the distro's agent is still starting (never waited for: this runs on the
		// UI thread), NotFound when the folder is in no repository.
		static Core::Result<Core::Scope<RepositoryIgnores>> Open(WslAgents& agents, const Core::WslSharePath& root, Lister list = Core::ReadDirectory);

		std::filesystem::path GetWorkingDirectory() const override { return m_Setup.WorkingDirectory; }
		bool IsIgnored(const std::filesystem::path& path, bool isDirectory) override;
		std::vector<std::filesystem::path> RuleFilesOutside(const std::filesystem::path& folder) override;
		void ForgetAnswers() override { m_Answers.clear(); }

	private:
		struct FolderAnswer
		{
			std::map<std::string, bool> IgnoredByName;
			bool Failed = false;
		};

		WslIgnores(std::unique_ptr<WslGit> git, WslGit::IgnoreSetup setup, Lister list);

		const FolderAnswer& Ask(const std::filesystem::path& folder);

		std::unique_ptr<WslGit> m_Git;
		WslGit::IgnoreSetup m_Setup;
		Lister m_List;
		std::map<std::filesystem::path, FolderAnswer> m_Answers;
		std::set<std::filesystem::path> m_Reported;
	};
}

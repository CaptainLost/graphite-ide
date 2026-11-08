#pragma once

#include "ScopedEnvironmentVariable.h"
#include "TempDirectory.h"

#include <filesystem>
#include <string>

// For as long as it lives, every git this process starts, and every process that git starts, reads no system config and
// a global config that is an empty file, so that nothing on the machine or in the user's account (init.defaultBranch,
// push.default, url.insteadOf, aliases) can change what a test sees. Declare it before the repositories a test makes.
class IsolatedGitConfig
{
public:
	IsolatedGitConfig()
		: m_GlobalConfig(m_Directory.MakeFile("empty-global-gitconfig")),
		  m_NoSystem(L"GIT_CONFIG_NOSYSTEM", std::wstring{L"1"}),
		  m_Global(L"GIT_CONFIG_GLOBAL", m_GlobalConfig.wstring())
	{
	}

	IsolatedGitConfig(const IsolatedGitConfig&) = delete;
	IsolatedGitConfig& operator=(const IsolatedGitConfig&) = delete;

private:
	TempDirectory m_Directory;
	std::filesystem::path m_GlobalConfig;
	ScopedEnvironmentVariable m_NoSystem;
	ScopedEnvironmentVariable m_Global;
};

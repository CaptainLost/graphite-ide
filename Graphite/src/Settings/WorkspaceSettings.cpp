#include "Settings/WorkspaceSettings.h"

#include "Persistence/FileBackup.h"
#include "Persistence/FolderStateDirectory.h"
#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/FileOperations.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kWorkspaceFileName = "workspace.json";
	}

	WorkspaceSettings::WorkspaceSettings(std::filesystem::path root) : m_Root(std::move(root))
	{
	}

	std::filesystem::path WorkspaceSettings::GetFilePath() const
	{
		return FolderStateDirectory(m_Root) / kWorkspaceFileName;
	}

	std::string WorkspaceSettings::Read(std::string_view key) const
	{
		if (m_Root.empty())
		{
			return {};
		}

		return Core::JsonFile::Load(GetFilePath()).Get(key);
	}

	void WorkspaceSettings::Write(std::string_view key, std::string value)
	{
		Modify([&](Core::JsonFile& file) { file.Set(key, std::move(value)); });
	}

	void WorkspaceSettings::Modify(const std::function<void(Core::JsonFile&)>& edit)
	{
		if (m_Root.empty() || !EnsureFolderStateDirectory(m_Root))
		{
			return;
		}

		Core::JsonFile file = Core::JsonFile::Load(GetFilePath());

		if (file.IsMalformed() && !BackUpBeforeWritingOver(GetFilePath(), "does not parse"))
		{
			APP_WARN("Not writing '{}': it does not parse and could not be copied aside", Core::ToUtf8(GetFilePath()));
			return;
		}

		edit(file);

		if (const Core::Result<void> saved = file.Save(GetFilePath()); !saved)
		{
			APP_WARN("Cannot write '{}': {}", Core::ToUtf8(GetFilePath()), saved.GetError().Message);
		}
	}

	WorkspaceState WorkspaceSettings::LoadState() const
	{
		if (m_Root.empty())
		{
			return {};
		}

		return ReadWorkspaceState(Core::JsonFile::Load(GetFilePath()), m_Root);
	}

	void WorkspaceSettings::SaveState(const WorkspaceState& state)
	{
		Modify([&](Core::JsonFile& file) { WriteWorkspaceState(state, m_Root, file); });
	}
}
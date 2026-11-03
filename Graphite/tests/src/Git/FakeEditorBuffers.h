#pragma once

#include "Git/EditorBuffers.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <vector>

// The editor as the git views see it: answers from a list of modified documents and records what it is asked to do.
class FakeEditorBuffers final : public Graphite::EditorBuffers
{
public:
	std::vector<std::filesystem::path> ModifiedUnder(const std::filesystem::path& root) const override
	{
		std::vector<std::filesystem::path> inside;

		for (const std::filesystem::path& modified : Modified)
		{
			if (Core::IsSameOrWithin(modified, root))
			{
				inside.push_back(modified);
			}
		}

		return inside;
	}

	bool Save(const std::vector<std::filesystem::path>& paths) override
	{
		Saved.insert(Saved.end(), paths.begin(), paths.end());
		return SaveSucceeds;
	}

	void Revert(const std::vector<std::filesystem::path>& paths) override
	{
		Reverted.insert(Reverted.end(), paths.begin(), paths.end());
	}

	void ExpectExternalWrite(const std::vector<std::filesystem::path>& paths) override
	{
		Expected.insert(Expected.end(), paths.begin(), paths.end());
	}

	std::vector<std::filesystem::path> Modified;
	bool SaveSucceeds = true;

	std::vector<std::filesystem::path> Saved;
	std::vector<std::filesystem::path> Reverted;
	std::vector<std::filesystem::path> Expected;
};

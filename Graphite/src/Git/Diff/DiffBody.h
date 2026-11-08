#pragma once

#include "Git/Diff/DiffView.h"
#include "Git/GitModel.h"
#include "Theme/GitPalette.h"

#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	struct OpenDiff
	{
		std::optional<Core::Oid> Commit;
		Core::DiffTarget Target = Core::DiffTarget::CommitToParent;
		std::filesystem::path Path;

		friend bool operator==(const OpenDiff&, const OpenDiff&) = default;
	};

	// What the model is asked for a working-tree diff: its side and its one file.
	inline Core::DiffRequest WorkingTreeRequestOf(const OpenDiff& diff)
	{
		return Core::DiffRequest{.Target = diff.Target, .File = diff.Path};
	}

	std::string EmptyDiffMessage(const OpenDiff& diff);

	std::optional<std::string> DiffFailureOf(const OpenDiff& diff, const GitModel& model);

	// WARNING: Forgets the failure before asking again, or the old failure stays on screen until the new answer lands.
	void RetryDiff(const OpenDiff& diff, GitModel& model);

	struct DiffBody
	{
		OpenDiff Diff;
		DiffView View;
		FileTextsBinding Texts;
	};

	struct DiffBodyStyle
	{
		const GitPalette& Palette;
		const SyntaxPalette& Syntax;
		ImU32 Selection = 0;
		DiffViewOptions Options;
		std::function<void(const std::filesystem::path&)> OpenFile;
		std::function<void(std::string_view)> OnCopied;
		std::function<void(const DiffViewOptions&)> OnSetOptions;
	};

	// Asks for the diff and draws it, its loader, or its failure with Retry, into the current window.
	// A working-tree body asks GitModel's single working-tree slot, so it asks only while it is drawn.
	void DrawDiffBody(DiffBody& body, GitModel& model, const DiffBodyStyle& style);
}

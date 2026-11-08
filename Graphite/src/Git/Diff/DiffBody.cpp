#include "Git/Diff/DiffBody.h"

#include "Git/History/CommitLabels.h"

#include "Core/Base/Base.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"

#include <optional>

namespace {

	constexpr const char* kRetry = "Retry";
}

namespace Graphite {

	void DrawDiffBody(DiffBody& body, GitModel& model, const DiffBodyStyle& style)
	{
		Core::Ref<const Core::Diff> loadedDiff;

		if (body.Diff.Commit.has_value())
		{
			model.RequestDiffFor(*body.Diff.Commit);
			loadedDiff = model.GetDiffFor(*body.Diff.Commit);
		}
		else
		{
			const Core::DiffRequest request = WorkingTreeRequestOf(body.Diff);
			model.RequestWorkingTreeDiff(request);
			loadedDiff = model.GetWorkingTreeDiff(request);
		}

		if (!loadedDiff)
		{
			if (const std::optional<std::string> failure = DiffFailureOf(body.Diff, model))
			{
				if (Core::EmptyState(*failure, kRetry))
				{
					RetryDiff(body.Diff, model);
				}
			}
			else
			{
				Core::LoadingState("Loading the diff");
			}

			return;
		}

		const std::optional<std::size_t> fileIndex = IndexOfFile(*loadedDiff, body.Diff.Path);

		if (!fileIndex.has_value())
		{
			Core::EmptyState(EmptyDiffMessage(body.Diff));
			return;
		}

		const Core::FileDiff& file = loadedDiff->Files[*fileIndex];

		if (body.Diff.Commit.has_value())
		{
			body.Texts.Update(model, *body.Diff.Commit, file);
		}
		else
		{
			body.Texts.UpdateFromWorkingTree(model, WorkingTreeRequestOf(body.Diff), file);
		}

		body.View.OnOpenFile = style.OpenFile;
		body.View.OnCopied = style.OnCopied;
		body.View.OnSetOptions = style.OnSetOptions;
		body.View.SetTextsWait(body.Texts.Wait);

		body.View.Render(file, style.Palette, style.Syntax, style.Selection, body.Texts.Texts,
						 body.Texts.Tokens, style.Options);
	}
}

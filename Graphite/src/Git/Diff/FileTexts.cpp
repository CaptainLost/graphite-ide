#include "Git/Diff/FileTexts.h"

#include "Core/Base/Base.h"

#include "Git/History/CommitLabels.h"
#include "Git/GitModel.h"

#include <utility>

namespace Graphite {

	namespace {

		TextsWait FailedWait(const std::optional<Core::Error>& failure)
		{
			return TextsWait{.Progress = TextsProgress::Failed,
							 .Reason = failure.has_value() ? failure->Message : std::string{}};
		}
	}

	Core::Result<SideText> ReadSideText(Core::Result<std::string> read)
	{
		if (read.HasValue())
		{
			constexpr bool present = true;

			return SideText{std::move(read).GetValue(), present};
		}

		const bool sideDidNotExist = read.GetError().Code == Core::ErrorCode::NotFound;

		if (sideDidNotExist)
		{
			return SideText{};
		}

		return read.GetError();
	}

	void FileTextsBinding::ForgetFailure(GitModel& model)
	{
		if (!Commit.has_value())
		{
			return;
		}

		model.ForgetFailedTextsFor(*Commit, Key);
	}

	void FileTextsBinding::Update(GitModel& model, const Core::Oid& commit,
								  const Core::FileDiff& file)
	{
		// WARNING: The key is ShownPath and not file.NewPath: a deleted file has no new path, so
		// keying by it would give every file deleted in one commit the same empty key and hand
		// the second one the first one's text.
		const std::filesystem::path& shownPath = ShownPath(file);

		if (!Commit.has_value() || *Commit != commit || Key != shownPath)
		{
			ForgetFailure(model);

			Commit = commit;
			Key = shownPath;
			Texts.reset();
			Tokens = FileTokens{};
		}

		Wait = TextsWait{};

		if (file.IsBinary)
		{
			// WARNING: A binary file's blobs are never fetched: nothing caps their size, the blob
			// read copies the object whole and the model's cache is bounded by entry count, so
			// clicking through a commit of large assets would retain hundreds of megabytes.
			Texts.reset();
			Tokens = FileTokens{};
			return;
		}

		model.RequestTextsFor(commit, shownPath, file.OldPath, file.NewPath);

		const Core::Ref<const FileTexts> texts = model.GetTextsFor(commit, shownPath);

		if (texts == nullptr)
		{
			Wait = model.IsTextsFailed(commit, shownPath) ? FailedWait(model.GetTextsFailure(commit, shownPath))
														   : TextsWait{.Progress = TextsProgress::Loading};
		}

		const bool sameTextsObject = texts.get() == Texts.get();

		if (sameTextsObject)
		{
			return;
		}

		Texts = texts;
		Tokens = texts != nullptr ? texts->Tokens : FileTokens{};
	}

	FileTokens ColourSides(const FileTexts& texts, const std::filesystem::path& oldPath,
						   const std::filesystem::path& newPath)
	{
		FileTokens tokens;

		if (texts.HasOld)
		{
			tokens.Old = ColourFileText(texts.Old, oldPath);
		}

		if (texts.HasNew)
		{
			tokens.New = ColourFileText(texts.New, newPath);
		}

		return tokens;
	}

	WorkingTreeSides SidesFor(Core::DiffTarget target)
	{
		switch (target)
		{
		case Core::DiffTarget::WorkTreeToIndex:
			return {.Old = TextSide::Index, .New = TextSide::WorkingTree};

		case Core::DiffTarget::IndexToHead:
			return {.Old = TextSide::Head, .New = TextSide::Index};

		case Core::DiffTarget::WorkTreeToHead:
			return {.Old = TextSide::Head, .New = TextSide::WorkingTree};

		case Core::DiffTarget::CommitToParent:
		case Core::DiffTarget::StashToParent:
		case Core::DiffTarget::RevisionToRevision:
			break;
		}

		const WorkingTreeSides noWorkingTreeSides;

		return noWorkingTreeSides;
	}

	bool TokensStillGood(const FileTexts* held, const FileTexts& fresh)
	{
		if (held == nullptr)
		{
			return false;
		}

		const bool samePresence = held->HasOld == fresh.HasOld && held->HasNew == fresh.HasNew;
		const bool sameBytes = held->Old == fresh.Old && held->New == fresh.New;

		return samePresence && sameBytes;
	}

	void FileTextsBinding::UpdateFromWorkingTree(GitModel& model, const Core::FileDiff& file)
	{
		const std::filesystem::path& shownPath = ShownPath(file);

		if (Key != shownPath)
		{
			Key = shownPath;
			Texts.reset();
			Tokens = FileTokens{};
		}

		Wait = TextsWait{};

		if (file.IsBinary)
		{
			Texts.reset();
			Tokens = FileTokens{};
			return;
		}

		const Core::Ref<const FileTexts> texts = model.GetWorkingTreeTexts();

		if (texts == nullptr)
		{
			// WARNING: The texts land with the diff they belong to, so beside a diff on screen they are either held or failed.
			if (const std::optional<Core::Error> failure = model.GetWorkingTreeTextsFailure())
			{
				Wait = FailedWait(failure);
			}

			Texts.reset();
			Tokens = FileTokens{};
			return;
		}

		if (TokensStillGood(Texts.get(), *texts))
		{
			Texts = texts;
			return;
		}

		Texts = texts;
		Tokens = texts->Tokens;
	}
}

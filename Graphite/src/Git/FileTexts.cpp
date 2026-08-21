#include "Git/FileTexts.h"

#include "Git/CommitLabels.h"
#include "Git/GitModel.h"

#include <utility>
#include <vector>

namespace Graphite {

	Core::Result<SideText> ReadSideText(Core::Result<std::string> read)
	{
		if (read.HasValue())
		{
			return SideText{std::move(read).GetValue(), true};
		}

		// NotFound is the ordinary answer for a side that did not exist in that commit,
		// not a failure - see the comment on SideText. Anything else is a real failure and
		// is passed through as one.
		if (read.GetError().Code == Core::ErrorCode::NotFound)
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
		// ShownPath's rule, and deliberately not file.NewPath: a deleted file has no new path
		// at all, so keying by it would give every file deleted in one commit the same empty
		// key and hand the second one the first one's text. The blob paths below stay as they
		// are — an empty one is exactly how ReadSideText learns a side is absent.
		const std::filesystem::path& key = ShownPath(file);

		if (!Commit.has_value() || *Commit != commit || Key != key)
		{
			// Only a failed entry is dropped, so a transient read failure does not go on
			// suppressing a retry for a file the reader may well come back to.
			ForgetFailure(model);

			Commit = commit;
			Key = key;
			Texts.reset();
			Tokens = FileTokens{};
		}

		if (file.IsBinary)
		{
			// Fetching a binary file's blobs would retain both of them behind a pane that says
			// "Binary file — no textual diff." Nothing on this path caps their size: the blob
			// read copies the object whole, and the model's cache is bounded by entry count
			// rather than by bytes, so a commit adding a few large assets could be clicked
			// through into hundreds of megabytes of text nothing can ever draw.
			Texts.reset();
			Tokens = FileTokens{};
			return;
		}

		model.RequestTextsFor(commit, key, file.OldPath, file.NewPath);

		const std::shared_ptr<const FileTexts> texts = model.GetTextsFor(commit, key);

		// The text is immutable once fetched, so an unchanged pointer means the tokens are
		// still good; re-lexing every frame is the mistake LexFileText's own header warns
		// against.
		if (texts.get() == Texts.get())
		{
			return;
		}

		Texts = texts;

		// Each side is lexed by its own path's language: a rename that changes the extension
		// really is two languages, one per side.
		Tokens.Old = texts && texts->HasOld ? LexFileText(texts->Old, file.OldPath)
										    : std::vector<std::vector<Token>>{};
		Tokens.New = texts && texts->HasNew ? LexFileText(texts->New, file.NewPath)
										    : std::vector<std::vector<Token>>{};
	}
}

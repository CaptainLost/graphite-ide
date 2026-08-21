#pragma once

#include "Git/SyntaxRuns.h"

#include "Core/Base/Result.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Graphite {

	class GitModel;

	// Both sides of one file's text around a commit. HasOld/HasNew exist because "absent
	// from that commit" and "an empty file" are different and both real: an added file has
	// no old side, a deleted file has no new side, and a genuinely empty file has a side
	// that is present and empty. A caller that cannot tell those apart would draw an added
	// file as an empty one.
	//
	// BuildWholeFileRows is not given these two flags: it takes line counts, and derives a
	// side's presence from its count being zero. That is not an oversight to plumb these
	// through for — an absent side and a side of zero lines lay out identically, since there
	// is nothing on either to lay out.
	struct FileTexts
	{
		std::string Old;
		std::string New;
		bool HasOld = false;
		bool HasNew = false;
	};

	// One side of a file: its text, and whether that side had the file at all. A NotFound
	// from the read means the file did not exist on that side - an added file has no old
	// side, a deleted one has no new side - which is an answer rather than a failure.
	// Anything else is a failure and stays one.
	struct SideText
	{
		std::string Text;
		bool Present = false;
	};

	// Turns a blob read into the answer ReadFileTexts needs for one side: present, absent,
	// or failed. Pulled out of RepositoryGitPort so this - the actual new reasoning this
	// task adds, as opposed to the port plumbing around it - can be tested without a
	// repository on disk.
	Core::Result<SideText> ReadSideText(Core::Result<std::string> read);

	// One owner's hold on a file's whole-file texts and the tokens lexed from them, plus the
	// rules for keeping the two in step with whichever file is on screen.
	//
	// It exists because both owners of a DiffView — the docked panel and a detached tab —
	// need exactly the same sequence, and what they share is not layout but rules: which path
	// keys the texts, when the tokens are still valid, and when a failed entry is forgiven.
	// Those were written out twice and were identically wrong in both copies, which is the
	// argument for there being one of them. It mentions neither owner, and DiffView never sees
	// it: the owner hands Texts and Tokens down as plain data, so the widget still knows
	// nothing about GitModel.
	struct FileTextsBinding
	{
		std::shared_ptr<const FileTexts> Texts;
		FileTokens Tokens;

		// Which file the two above belong to. Key is ShownPath's answer for the file — the
		// path that names it, which is the new path except for a delete, where there is none.
		// Commit is unset until the first Update, and is held alongside Key because forgiving
		// a failed entry for the file just left needs the commit that entry was keyed under,
		// not the one now on screen.
		std::optional<Core::Oid> Commit;
		std::filesystem::path Key;

		// Points this binding at `file` as it stood in `commit`, asks the model for its texts,
		// and re-lexes only when the model hands back a different object than last time — the
		// text is immutable once fetched, so the pointer alone says whether the tokens are
		// still good, and re-lexing every frame is the mistake LexFileText's own header warns
		// against. Safe to call every frame; that is how it is meant to be called.
		//
		// A binary file is not fetched, cached or lexed at all: its bytes can never be drawn
		// as text, nothing on this path caps how many of them there are, and the pane says so
		// without needing any of them.
		void Update(GitModel& model, const Core::Oid& commit, const Core::FileDiff& file);

		// Drops the model's entry for the file this binding points at, but only if that entry
		// failed — GitModel::ForgetFailedTextsFor is a no-op otherwise. Called by Update when
		// the binding moves to a different file, and by an owner whose view is going away, so
		// a transient read failure does not go on suppressing a retry forever.
		void ForgetFailure(GitModel& model);
	};
}

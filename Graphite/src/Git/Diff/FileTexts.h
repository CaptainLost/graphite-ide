#pragma once

#include "Core/Base/Base.h"

#include "Git/Diff/SyntaxRuns.h"

#include "Core/Base/Result.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

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

		// Filled by the model's job that reads the texts, not by the port, and never on the UI
		// thread: a C# side is a whole-file parse.
		FileTokens Tokens;
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

	// Where one side of a working-tree diff's text comes from. A commit is the one thing
	// neither side of such a diff is, which is why ReadBlob alone cannot serve them.
	enum class TextSide
	{
		// Not a working-tree diff at all, so there is nothing here to read.
		Absent,

		Head,
		Index,
		WorkingTree
	};

	struct WorkingTreeSides
	{
		TextSide Old = TextSide::Absent;
		TextSide New = TextSide::Absent;
	};

	// Which two sources a target compares. Pulled out of the port as a function of the
	// target alone, because this is the actual new reasoning - "unstaged means the index
	// against the disk" - as opposed to the reading around it, and it is checkable without a
	// repository, a thread or a file.
	WorkingTreeSides SidesFor(Core::DiffTarget target);

	// Each present side by its own path's language, since a rename that changes the extension
	// really is two languages. It parses whole sides, so call it only from the job that read them.
	FileTokens ColourSides(const FileTexts& texts, const std::filesystem::path& oldPath,
						   const std::filesystem::path& newPath);

	// A working-tree refresh brings a new object even when the file has not changed, so this
	// compares both sides' bytes and presence, never the pointer.
	bool TokensStillGood(const FileTexts* held, const FileTexts& fresh);

	// One owner's hold on a file's whole-file texts and the tokens that came with them, plus the
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
		Core::Ref<const FileTexts> Texts;
		FileTokens Tokens;

		// Which file the two above belong to. Key is ShownPath's answer for the file — the
		// path that names it, which is the new path except for a delete, where there is none.
		// Commit is unset until the first Update, and is held alongside Key because forgiving
		// a failed entry for the file just left needs the commit that entry was keyed under,
		// not the one now on screen.
		std::optional<Core::Oid> Commit;
		std::filesystem::path Key;

		// Called every frame, so it copies the tokens only when the model hands back a different
		// texts object; copying a whole file's tokens every frame would cost the frame.
		//
		// A binary file is not fetched, cached or coloured at all: its bytes can never be drawn
		// as text, nothing on this path caps how many of them there are, and the pane says so
		// without needing any of them.
		void Update(GitModel& model, const Core::Oid& commit, const Core::FileDiff& file);

		// The same job for the working tree's own text, which has no commit to be keyed by and
		// arrives as a single slot the model replaces. Copies the tokens only when the text
		// actually differs - see TokensStillGood.
		void UpdateFromWorkingTree(GitModel& model, const Core::FileDiff& file);

		// Drops the model's entry for the file this binding points at, but only if that entry
		// failed — GitModel::ForgetFailedTextsFor is a no-op otherwise. Called by Update when
		// the binding moves to a different file, and by an owner whose view is going away, so
		// a transient read failure does not go on suppressing a retry forever.
		void ForgetFailure(GitModel& model);
	};
}

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

	struct FileTexts
	{
		std::string Old;
		std::string New;
		// WARNING: A side that is absent from the commit is not an empty file: an added file has
		// no old side, a deleted one has no new side, and an empty file has a side that is
		// present and empty. Treating them alike draws an added file as an empty one.
		bool HasOld = false;
		bool HasNew = false;

		// WARNING: Filled by the model's job that reads the texts, never on the UI thread: a C#
		// side is a whole-file parse.
		FileTokens Tokens;
	};

	struct SideText
	{
		std::string Text;
		bool Present = false;
	};

	Core::Result<SideText> ReadSideText(Core::Result<std::string> read);

	enum class TextSide
	{
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

	WorkingTreeSides SidesFor(Core::DiffTarget target);

	// WARNING: It parses whole sides, so call it only from the job that read them.
	FileTokens ColourSides(const FileTexts& texts, const std::filesystem::path& oldPath,
						   const std::filesystem::path& newPath);

	// WARNING: A working-tree refresh brings a new object even when the file has not changed,
	// so this compares both sides' bytes and presence, never the pointer.
	bool TokensStillGood(const FileTexts* held, const FileTexts& fresh);

	enum class TextsProgress
	{
		// Nothing is known or nothing is waited for: held, binary, or an owner that does not say.
		Unknown,
		Loading,
		Failed
	};

	// What became of a file's texts that are not held yet, for the diff's toolbar to say.
	struct TextsWait
	{
		TextsProgress Progress = TextsProgress::Unknown;

		// Why they failed, when the failure gave a reason.
		std::string Reason;

		friend bool operator==(const TextsWait&, const TextsWait&) = default;
	};

	struct FileTextsBinding
	{
		Core::Ref<const FileTexts> Texts;
		FileTokens Tokens;

		// Set by Update and UpdateFromWorkingTree while Texts is null.
		TextsWait Wait;

		// WARNING: Commit is held alongside Key because forgiving a failed entry for the file
		// just left needs the commit that entry was keyed under, not the one now on screen.
		std::optional<Core::Oid> Commit;
		std::filesystem::path Key;

		// WARNING: Called every frame, so it copies the tokens only when the model hands back a
		// different texts object.
		void Update(GitModel& model, const Core::Oid& commit, const Core::FileDiff& file);

		void UpdateFromWorkingTree(GitModel& model, const Core::FileDiff& file);

		void ForgetFailure(GitModel& model);
	};
}

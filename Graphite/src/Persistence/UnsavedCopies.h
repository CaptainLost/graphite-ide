#pragma once

#include "Core/Jobs/JobSubmitter.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {
	class DocumentStore;
	class FileLock;
	class TextDocument;
}

namespace Graphite {

	// A document's unsaved text as a launch that is no longer running last copied it.
	struct UnsavedCopy
	{
		std::filesystem::path Path;
		std::string Text;

		// The copy's own file, named to the reader when the text cannot be put back.
		std::filesystem::path KeptIn;
	};

	// Keeps a copy of every open document's unsaved text in a directory of this launch's own, so a crash or a kill
	// loses at most a few seconds of typing, and hands the next launch what a launch no longer running left behind
	// (ADR 0684).
	class UnsavedCopies
	{
	public:
		// A document is copied once its text has rested this long, or after the longer wait while it keeps changing.
		static constexpr double kRestSeconds = 1.0;
		static constexpr double kLongestUncopiedSeconds = 5.0;

		// A larger document is not copied: materialising it every few seconds would cost more than the copy is worth.
		static constexpr std::size_t kLargestCopiedBytes = std::size_t{64} << 20;

		// WARNING: Writes and deletions are submitted in the order they must happen, so one worker has to take them.
		UnsavedCopies(std::filesystem::path directory, Core::JobSubmitter submit);
		~UnsavedCopies();

		UnsavedCopies(const UnsavedCopies&) = delete;
		UnsavedCopies& operator=(const UnsavedCopies&) = delete;

		// Every copy left by a launch no longer running. Each is kept until this launch has copied its document again,
		// or the document is no longer modified, so a crash right after the restart loses nothing either.
		std::vector<UnsavedCopy> ClaimLeftBehind();

		void Observe(double elapsedSeconds, const Core::DocumentStore& store);

		// Copies every modified document now, without waiting for it to rest, and keeps every copy for the next launch:
		// for an exit nobody was asked about.
		void KeepForNextLaunch(const Core::DocumentStore& store);

		// For an exit whose unsaved changes the reader saved or discarded: this launch's copies go, and a claimed copy
		// stays only when its document never came back.
		void Discard();

	private:
		struct Copied
		{
			std::uint64_t Seen = 0;
			double SeenAt = 0.0;
			double UncopiedSince = 0.0;
			std::optional<std::uint64_t> Written;
			std::filesystem::path File;
		};

		struct ClaimedCopy
		{
			std::filesystem::path Path;
			std::filesystem::path File;
		};

		struct Claim
		{
			std::filesystem::path Directory;
			std::shared_ptr<Core::FileLock> Lock;
			std::vector<ClaimedCopy> Copies;
		};

		bool EnsureOwnDirectory();
		void Write(const std::filesystem::path& path, const Core::TextDocument& document, Copied& copied);
		void Forget(std::map<std::filesystem::path, Copied>::iterator copied);
		void SettleClaims(const Core::DocumentStore& store);
		void LetGoOf(Claim& claim);

		std::filesystem::path m_Directory;
		Core::JobSubmitter m_Submit;

		std::string m_OwnName;
		std::shared_ptr<Core::FileLock> m_OwnLock;
		bool m_CannotCopy = false;
		std::uint64_t m_LastFileNumber = 0;

		double m_Clock = 0.0;
		std::map<std::filesystem::path, Copied> m_Copied;
		std::vector<Claim> m_Claims;
	};

	// Puts each copy's text into its document, loaded into the store if no tab holds it yet, as one change Undo
	// takes back to the file on disk. The documents are returned so they live until a tab holds each; a copy whose
	// file cannot be read is returned with the reason.
	struct PutBackUnsaved
	{
		std::vector<std::shared_ptr<Core::TextDocument>> Documents;

		struct Failure
		{
			UnsavedCopy Copy;
			std::string Reason;
		};

		std::vector<Failure> Failures;
	};

	PutBackUnsaved PutBack(Core::DocumentStore& store, const std::vector<UnsavedCopy>& copies);

	// The copy's file as it lands on disk, and read back; nothing when the bytes are not a copy.
	std::string EncodeUnsavedCopy(const std::filesystem::path& path, std::string_view text);
	std::optional<UnsavedCopy> DecodeUnsavedCopy(std::string_view bytes);
}

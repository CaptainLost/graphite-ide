#include "Persistence/UnsavedCopies.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileLock.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/DocumentStore.h"
#include "Core/Text/TextDocument.h"

#include <algorithm>
#include <format>
#include <random>
#include <set>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kCopyHeader = "graphite unsaved copy 1";
		constexpr std::string_view kCopyExtension = ".copy";
		constexpr std::string_view kLockName = "lock";

		std::string RandomName()
		{
			std::random_device source;
			std::uniform_int_distribution<std::uint64_t> digits;

			return std::format("{:016x}", digits(source));
		}

		void RemoveQuietly(const std::filesystem::path& file)
		{
			std::error_code removeError;
			std::filesystem::remove(file, removeError);
		}

		// WARNING: The lock is let go of first: its file is deleted as its handle closes, and a directory still holding
		// it cannot be removed.
		void RemoveDirectoryAfterLock(const std::filesystem::path& directory, std::shared_ptr<Core::FileLock>& lock)
		{
			lock.reset();

			std::error_code removeError;
			std::filesystem::remove_all(directory, removeError);

			if (removeError)
			{
				APP_WARN("Cannot remove the unsaved copies in '{}': {}", Core::ToUtf8(directory), removeError.message());
			}
		}

		std::vector<std::filesystem::path> CopyFilesIn(const std::filesystem::path& directory)
		{
			std::vector<std::filesystem::path> files;
			std::error_code listError;

			for (std::filesystem::directory_iterator entry{directory, listError}, end; !listError && entry != end;
				 entry.increment(listError))
			{
				if (entry->path().extension() == kCopyExtension)
				{
					files.push_back(entry->path());
				}
			}

			std::sort(files.begin(), files.end());
			return files;
		}
	}

	std::string EncodeUnsavedCopy(const std::filesystem::path& path, std::string_view text)
	{
		std::string bytes;
		bytes.reserve(kCopyHeader.size() + text.size() + 256);
		bytes += kCopyHeader;
		bytes += '\n';
		bytes += Core::ToUtf8(path);
		bytes += '\n';
		bytes += text;

		return bytes;
	}

	std::optional<UnsavedCopy> DecodeUnsavedCopy(std::string_view bytes)
	{
		const std::size_t headerEnd = bytes.find('\n');

		if (headerEnd == std::string_view::npos || bytes.substr(0, headerEnd) != kCopyHeader)
		{
			return std::nullopt;
		}

		const std::size_t pathStart = headerEnd + 1;
		const std::size_t pathEnd = bytes.find('\n', pathStart);

		if (pathEnd == std::string_view::npos || pathEnd == pathStart)
		{
			return std::nullopt;
		}

		return UnsavedCopy{.Path = Core::FromUtf8(bytes.substr(pathStart, pathEnd - pathStart)),
						   .Text = std::string{bytes.substr(pathEnd + 1)}};
	}

	UnsavedCopies::UnsavedCopies(std::filesystem::path directory, Core::JobSubmitter submit)
		: m_Directory(std::move(directory)),
		  m_Submit(std::move(submit)),
		  m_OwnName(RandomName())
	{
	}

	UnsavedCopies::~UnsavedCopies() = default;

	std::vector<UnsavedCopy> UnsavedCopies::ClaimLeftBehind()
	{
		std::vector<UnsavedCopy> found;
		std::error_code listError;

		for (std::filesystem::directory_iterator entry{m_Directory, listError}, end; !listError && entry != end;
			 entry.increment(listError))
		{
			std::error_code kindError;

			if (!entry->is_directory(kindError) || entry->path().filename() == m_OwnName)
			{
				continue;
			}

			// Held means that launch is still running; a launch that died let go of it with its last handle.
			Core::Result<Core::FileLock> lock = Core::FileLock::TryAcquire(entry->path() / kLockName);

			if (!lock)
			{
				continue;
			}

			Claim claim{.Directory = entry->path(),
						.Lock = std::make_shared<Core::FileLock>(std::move(lock).GetValue())};

			for (const std::filesystem::path& file : CopyFilesIn(claim.Directory))
			{
				const Core::Result<std::string> bytes = Core::ReadFileBytes(file);
				std::optional<UnsavedCopy> copy = bytes ? DecodeUnsavedCopy(bytes.GetValue()) : std::nullopt;

				if (!copy)
				{
					APP_WARN("'{}' is not an unsaved copy Graphite can read; left where it is", Core::ToUtf8(file));
					continue;
				}

				copy->KeptIn = file;
				claim.Copies.push_back(ClaimedCopy{.Path = copy->Path, .File = file});
				found.push_back(std::move(*copy));
			}

			if (claim.Copies.empty())
			{
				LetGoOf(claim);
				continue;
			}

			m_Claims.push_back(std::move(claim));
		}

		return found;
	}

	void UnsavedCopies::Observe(double elapsedSeconds, const Core::DocumentStore& store)
	{
		m_Clock += elapsedSeconds;

		std::set<std::filesystem::path> open;

		for (const std::shared_ptr<Core::TextDocument>& document : store.GetDocuments())
		{
			const std::filesystem::path& path = document->GetPath();
			open.insert(path);

			auto copied = m_Copied.find(path);
			const bool worthCopying = document->IsModified() && document->GetBuffer().GetLength() <= kLargestCopiedBytes;

			if (!worthCopying)
			{
				if (copied != m_Copied.end())
				{
					Forget(copied);
				}

				continue;
			}

			const std::uint64_t sequence = document->GetBuffer().GetChangeSequence();

			if (copied == m_Copied.end())
			{
				copied = m_Copied.emplace(path, Copied{.Seen = sequence, .SeenAt = m_Clock, .UncopiedSince = m_Clock}).first;
			}

			Copied& state = copied->second;

			if (state.Seen != sequence)
			{
				const bool wasCopied = state.Written == state.Seen;

				state.Seen = sequence;
				state.SeenAt = m_Clock;

				if (wasCopied)
				{
					state.UncopiedSince = m_Clock;
				}
			}

			if (state.Written == sequence)
			{
				continue;
			}

			const bool rested = m_Clock - state.SeenAt >= kRestSeconds;
			const bool waitedLongest = m_Clock - state.UncopiedSince >= kLongestUncopiedSeconds;

			if (rested || waitedLongest)
			{
				Write(path, *document, state);
			}
		}

		for (auto copied = m_Copied.begin(); copied != m_Copied.end();)
		{
			if (open.contains(copied->first))
			{
				++copied;
				continue;
			}

			auto closed = copied++;
			Forget(closed);
		}

		SettleClaims(store);
	}

	void UnsavedCopies::KeepForNextLaunch(const Core::DocumentStore& store)
	{
		for (const std::shared_ptr<Core::TextDocument>& document : store.GetDocuments())
		{
			if (!document->IsModified() || document->GetBuffer().GetLength() > kLargestCopiedBytes)
			{
				continue;
			}

			const std::uint64_t sequence = document->GetBuffer().GetChangeSequence();
			Copied& state = m_Copied[document->GetPath()];

			if (state.Written != sequence)
			{
				state.Seen = sequence;
				Write(document->GetPath(), *document, state);
			}
		}
	}

	void UnsavedCopies::Discard()
	{
		if (m_OwnLock)
		{
			m_Submit([directory = m_Directory / m_OwnName, lock = std::move(m_OwnLock)]() mutable {
				RemoveDirectoryAfterLock(directory, lock);
			});
		}

		m_Copied.clear();

		// A claimed copy still here is one whose document never came back, so it waits for the next launch.
		for (Claim& claim : m_Claims)
		{
			m_Submit([lock = std::move(claim.Lock)]() mutable { lock.reset(); });
		}

		m_Claims.clear();
	}

	bool UnsavedCopies::EnsureOwnDirectory()
	{
		if (m_OwnLock)
		{
			return true;
		}

		if (m_CannotCopy)
		{
			return false;
		}

		const std::filesystem::path own = m_Directory / m_OwnName;
		std::error_code createError;
		std::filesystem::create_directories(own, createError);

		Core::Result<Core::FileLock> lock =
			createError ? Core::Result<Core::FileLock>{Core::Error{.Code = Core::ErrorCode::IoFailure, .Message = createError.message()}}
						: Core::FileLock::TryAcquire(own / kLockName);

		if (!lock)
		{
			// WARNING: Logged once: every later frame would ask again and fail the same way.
			APP_WARN("Unsaved changes are not copied, so a crash would lose them: cannot use '{}': {}", Core::ToUtf8(own),
					 lock.GetError().Message);
			m_CannotCopy = true;
			return false;
		}

		m_OwnLock = std::make_shared<Core::FileLock>(std::move(lock).GetValue());
		return true;
	}

	void UnsavedCopies::Write(const std::filesystem::path& path, const Core::TextDocument& document, Copied& copied)
	{
		if (!EnsureOwnDirectory())
		{
			return;
		}

		if (copied.File.empty())
		{
			copied.File = m_Directory / m_OwnName / std::format("{}{}", ++m_LastFileNumber, kCopyExtension);
		}

		copied.Written = copied.Seen;

		m_Submit([file = copied.File, path, text = document.GetBuffer().Materialize()] {
			const Core::Result<void> written = Core::WriteFileAtomically(file, EncodeUnsavedCopy(path, text));

			if (!written)
			{
				APP_WARN("Cannot copy the unsaved changes to '{}': {}", Core::ToUtf8(path), written.GetError().Message);
			}
		});
	}

	void UnsavedCopies::Forget(std::map<std::filesystem::path, Copied>::iterator copied)
	{
		if (copied->second.Written)
		{
			m_Submit([file = copied->second.File] { RemoveQuietly(file); });
		}

		m_Copied.erase(copied);
	}

	void UnsavedCopies::SettleClaims(const Core::DocumentStore& store)
	{
		for (auto claim = m_Claims.begin(); claim != m_Claims.end();)
		{
			std::erase_if(claim->Copies, [this, &store](const ClaimedCopy& copy) {
				const std::shared_ptr<Core::TextDocument> document = store.Find(copy.Path);

				if (document == nullptr)
				{
					return false;
				}

				const auto copied = m_Copied.find(copy.Path);
				const bool copiedAgain = copied != m_Copied.end() && copied->second.Written.has_value();
				const bool settled = !document->IsModified() || copiedAgain;

				if (settled)
				{
					m_Submit([file = copy.File] { RemoveQuietly(file); });
				}

				return settled;
			});

			if (!claim->Copies.empty())
			{
				++claim;
				continue;
			}

			LetGoOf(*claim);
			claim = m_Claims.erase(claim);
		}
	}

	void UnsavedCopies::LetGoOf(Claim& claim)
	{
		m_Submit([directory = claim.Directory, lock = std::move(claim.Lock)]() mutable {
			RemoveDirectoryAfterLock(directory, lock);
		});
	}

	PutBackUnsaved PutBack(Core::DocumentStore& store, const std::vector<UnsavedCopy>& copies)
	{
		PutBackUnsaved putBack;

		for (const UnsavedCopy& copy : copies)
		{
			Core::Result<std::shared_ptr<Core::TextDocument>> acquired = store.Acquire(copy.Path);

			if (!acquired)
			{
				putBack.Failures.push_back({.Copy = copy, .Reason = acquired.GetError().Message});
				continue;
			}

			std::shared_ptr<Core::TextDocument> document = std::move(acquired).GetValue();

			if (document->IsReadOnly())
			{
				putBack.Failures.push_back({.Copy = copy, .Reason = "the file is open read-only"});
				continue;
			}

			// The same text as on disk leaves the document unmodified, and its copy is let go of for that.
			if (document->GetBuffer().Materialize() != copy.Text)
			{
				document->GetBuffer().ReplaceContent(copy.Text);
			}

			putBack.Documents.push_back(std::move(document));
		}

		return putBack;
	}
}

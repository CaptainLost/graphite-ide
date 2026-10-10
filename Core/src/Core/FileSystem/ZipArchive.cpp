#include "corepch.h"

#include "Core/FileSystem/ZipArchive.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/RelativePath.h"

#include <miniz.h>

#include <fstream>
#include <system_error>

namespace Core {

	namespace {

		// An initialised reader holds the archive open, so it is ended on every path out. miniz widens a UTF-8
		// name before opening it on Windows, which is what lets a path outside the ANSI code page open.
		class ZipReader
		{
		public:
			ZipReader() = default;
			ZipReader(const ZipReader&) = delete;
			ZipReader& operator=(const ZipReader&) = delete;

			~ZipReader()
			{
				if (m_IsOpen)
				{
					mz_zip_reader_end(&m_Archive);
				}
			}

			bool Open(const std::filesystem::path& archive)
			{
				m_IsOpen = mz_zip_reader_init_file(&m_Archive, ToUtf8(archive).c_str(), 0) != MZ_FALSE;
				return m_IsOpen;
			}

			mz_zip_archive* Get() { return &m_Archive; }

			std::string_view LastError() { return mz_zip_get_error_string(mz_zip_get_last_error(&m_Archive)); }

		private:
			mz_zip_archive m_Archive{};
			bool m_IsOpen = false;
		};

		struct EntryOutput
		{
			std::ofstream* Stream = nullptr;
			const CancellationToken* Token = nullptr;
		};

		// Answering fewer bytes than it was given makes miniz stop, which is how a cancel ends an entry midway.
		std::size_t WriteEntryPiece(void* opaque, mz_uint64, const void* bytes, std::size_t byteCount)
		{
			const EntryOutput* output = static_cast<const EntryOutput*>(opaque);

			if (output->Token->IsCancelled())
			{
				return 0;
			}

			output->Stream->write(static_cast<const char*>(bytes), static_cast<std::streamsize>(byteCount));
			return output->Stream->good() ? byteCount : 0;
		}

		Error CancelledError(const std::filesystem::path& archive)
		{
			return Error{.Code = ErrorCode::Cancelled,
						 .Message = std::format("extracting '{}' was cancelled", ToUtf8(archive))};
		}

		Result<void> CreateDirectories(const std::filesystem::path& directory)
		{
			std::error_code creationError;
			std::filesystem::create_directories(directory, creationError);

			if (creationError)
			{
				return Error{.Code = ErrorCode::IoFailure,
							 .Message = std::format("cannot create '{}': {}", ToUtf8(directory), creationError.message())};
			}

			return Result<void>::Ok();
		}

		Result<std::vector<mz_zip_archive_file_stat>> ListSafeEntries(ZipReader& reader,
																	  const std::filesystem::path& archive)
		{
			const mz_uint entryCount = mz_zip_reader_get_num_files(reader.Get());
			std::vector<mz_zip_archive_file_stat> entries;
			entries.reserve(entryCount);

			for (mz_uint entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				mz_zip_archive_file_stat entry{};

				if (mz_zip_reader_file_stat(reader.Get(), entryIndex, &entry) == MZ_FALSE)
				{
					return Error{.Code = ErrorCode::IoFailure,
								 .Message = std::format("cannot read entry {} of '{}': {}", entryIndex, ToUtf8(archive),
														reader.LastError())};
				}

				if (!IsSafeRelativePath(entry.m_filename))
				{
					return Error{.Code = ErrorCode::InvalidArgument,
								 .Message = std::format("'{}' holds an entry named '{}', which could land outside the "
														"folder it is extracted into",
														ToUtf8(archive), entry.m_filename)};
				}

				if (entry.m_is_encrypted != MZ_FALSE || entry.m_is_supported == MZ_FALSE)
				{
					return Error{.Code = ErrorCode::Unsupported,
								 .Message = std::format("'{}' holds an entry '{}' that is encrypted or compressed in a "
														"way this build cannot read",
														ToUtf8(archive), entry.m_filename)};
				}

				entries.push_back(entry);
			}

			return entries;
		}

		Result<void> ExtractEntry(ZipReader& reader, const mz_zip_archive_file_stat& entry,
								  const std::filesystem::path& target, const std::filesystem::path& archive,
								  const CancellationToken& token)
		{
			if (const Result<void> created = CreateDirectories(target.parent_path()); !created)
			{
				return created;
			}

			// Closed when this returns, so the caller can remove what was written.
			std::ofstream stream{target, std::ios::binary | std::ios::trunc};

			if (!stream.is_open())
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot write '{}'", ToUtf8(target))};
			}

			EntryOutput output{.Stream = &stream, .Token = &token};
			const bool extracted =
				mz_zip_reader_extract_to_callback(reader.Get(), entry.m_file_index, WriteEntryPiece, &output, 0) != MZ_FALSE;

			if (token.IsCancelled())
			{
				return CancelledError(archive);
			}

			if (!extracted || !stream.good())
			{
				return Error{.Code = ErrorCode::IoFailure,
							 .Message = std::format("cannot extract '{}' to '{}': {}", entry.m_filename, ToUtf8(target),
													reader.LastError())};
			}

			return Result<void>::Ok();
		}
	}

	Result<void> ExtractZip(const std::filesystem::path& archive, const std::filesystem::path& destination,
							const CancellationToken& token)
	{
		ZipReader reader;

		if (!reader.Open(archive))
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot read '{}' as a zip archive: {}", ToUtf8(archive), reader.LastError())};
		}

		const Result<std::vector<mz_zip_archive_file_stat>> entries = ListSafeEntries(reader, archive);

		if (!entries)
		{
			return entries.GetError();
		}

		if (const Result<void> created = CreateDirectories(destination); !created)
		{
			return created;
		}

		for (const mz_zip_archive_file_stat& entry : entries.GetValue())
		{
			if (token.IsCancelled())
			{
				return CancelledError(archive);
			}

			const std::filesystem::path target = destination / FromUtf8(entry.m_filename);
			const bool isDirectory = entry.m_is_directory != MZ_FALSE;
			const Result<void> extracted = isDirectory ? CreateDirectories(target)
													   : ExtractEntry(reader, entry, target, archive, token);

			if (!extracted)
			{
				return extracted;
			}
		}

		return Result<void>::Ok();
	}
}

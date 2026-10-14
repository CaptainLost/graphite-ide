#include "Language/Analysis/FileTextSource.h"

#include "Core/App/Redraw.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Codec.h"
#include "Core/Text/FileProbe.h"

#include <optional>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		std::optional<std::filesystem::file_time_type> ReadStamp(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, error);

			if (error)
			{
				return std::nullopt;
			}

			return modified;
		}

		bool IsNotAnalysableKind(Core::FileKind kind)
		{
			return kind == Core::FileKind::Binary || kind == Core::FileKind::TooLarge || kind == Core::FileKind::LargeText;
		}
	}

	FileText ReadFileText(const std::filesystem::path& path)
	{
		return ReadFileText(path, ReadStamp);
	}

	FileText ReadFileText(const std::filesystem::path& path, const FileStampReader& readStamp)
	{
		FileText read;
		read.Path = path;

		const Core::Result<Core::FileProbe> probed = Core::ProbeFile(path);

		if (!probed)
		{
			return read;
		}

		const Core::FileProbe& probe = probed.GetValue();

		if (IsNotAnalysableKind(probe.Kind))
		{
			return read;
		}

		// WARNING: The stamp is read before the bytes and again after them. A save landing in
		// between would pair the old text with the new time, so two differing stamps mean the file
		// is reported unreadable and requested again.
		const std::optional<std::filesystem::file_time_type> stampBefore = readStamp(path);

		if (!stampBefore)
		{
			return read;
		}

		Core::Result<std::string> file = Core::ReadFileBytes(path, Core::MaximumFileSize);

		if (!file)
		{
			return read;
		}

		Core::Result<std::string> text = Core::DecodeWithoutByteOrderMark(std::move(file.GetValue()), probe.FileEncoding);

		if (!text)
		{
			return read;
		}

		const std::optional<std::filesystem::file_time_type> stampAfter = readStamp(path);

		if (!stampAfter || *stampAfter != *stampBefore)
		{
			return read;
		}

		read.Text = std::move(text.GetValue());
		read.ModifiedAt = *stampBefore;
		read.Readable = true;

		return read;
	}

	void DiskFileTexts::Request(const std::filesystem::path& path)
	{
		m_Jobs.Submit([ready = m_Ready, path] {
			FileText read = ReadFileText(path);

			{
				const std::lock_guard<std::mutex> lock(ready->Mutex);
				ready->Texts.push_back(std::move(read));
			}

			Core::Redraw::Request();
		});
	}

	void DiskFileTexts::Take(std::vector<FileText>& destination)
	{
		const std::lock_guard<std::mutex> lock(m_Ready->Mutex);

		for (FileText& text : m_Ready->Texts)
		{
			destination.push_back(std::move(text));
		}

		m_Ready->Texts.clear();
	}
}

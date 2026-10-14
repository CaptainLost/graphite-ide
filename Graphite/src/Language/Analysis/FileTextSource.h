#pragma once

#include "Core/Base/Base.h"

#include "Core/Jobs/JobQueue.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct FileText
	{
		std::filesystem::path Path;
		std::string Text;
		std::filesystem::file_time_type ModifiedAt{};

		bool Readable = false;
	};

	FileText ReadFileText(const std::filesystem::path& path);

	using FileStampReader =
		std::function<std::optional<std::filesystem::file_time_type>(const std::filesystem::path&)>;

	FileText ReadFileText(const std::filesystem::path& path, const FileStampReader& readStamp);

	class FileTextSource
	{
	public:
		virtual ~FileTextSource() = default;

		FileTextSource() = default;
		FileTextSource(const FileTextSource&) = delete;
		FileTextSource& operator=(const FileTextSource&) = delete;

		virtual void Request(const std::filesystem::path& path) = 0;

		virtual void Take(std::vector<FileText>& destination) = 0;
	};

	class DiskFileTexts final : public FileTextSource
	{
	public:
		void Request(const std::filesystem::path& path) override;

		void Take(std::vector<FileText>& destination) override;

	private:
		// WARNING: Held through a shared_ptr so a read still finishing as this object goes away
		// has a valid block to write to.
		struct Ready
		{
			std::mutex Mutex;
			std::vector<FileText> Texts;
		};

		Core::Ref<Ready> m_Ready = Core::CreateRef<Ready>();

		// WARNING: Declared last so the worker is joined before the members it touches are destroyed.
		Core::JobQueue m_Jobs{1};
	};
}

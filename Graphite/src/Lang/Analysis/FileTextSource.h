#pragma once

#include "Core/Jobs/JobQueue.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Graphite {

	// One file as the background analysis needs it: its decoded text and the stamp that text was
	// true at. The stamp is read beside the bytes rather than when the answer comes back, so an
	// entry can only ever vouch for the version that was actually sent to the server.
	struct FileText
	{
		std::filesystem::path Path;
		std::string Text;
		std::filesystem::file_time_type ModifiedAt{};

		// False for a file that could not be opened, could not be decoded, or is not text at all.
		// Text is empty in every such case, and the caller drops the file rather than telling a
		// server about a PNG.
		bool Readable = false;
	};

	// Probes, reads, decodes and stamps one file. Pure enough to test against a real directory and
	// nothing else — which is why the class below is a thin shell around it.
	//
	// Never called on the UI thread: a file of unknown size opened inside a frame is exactly what
	// this codebase does not do.
	FileText ReadFileText(const std::filesystem::path& path);

	// What WorkspaceAnalysis needs in order to have a file's text without reading one itself.
	//
	// A port rather than a JobQueue member, for the reason PreviewServerPort exists: the policy
	// above it — one flight at a time, yield to the open document, cache what came back — is
	// worth asserting without a filesystem, a thread, or a wait in a test.
	class FileTextSource
	{
	public:
		virtual ~FileTextSource() = default;

		FileTextSource() = default;
		FileTextSource(const FileTextSource&) = delete;
		FileTextSource& operator=(const FileTextSource&) = delete;

		virtual void Request(const std::filesystem::path& path) = 0;

		// Moves everything read since the last call into `out`. UI thread only.
		virtual void Take(std::vector<FileText>& out) = 0;
	};

	// The production source: one worker reading whatever has been asked for.
	class DiskFileTexts final : public FileTextSource
	{
	public:
		void Request(const std::filesystem::path& path) override;

		void Take(std::vector<FileText>& out) override;

	private:
		// Held through a shared_ptr, as LanguageServices holds its warm-up findings and for the
		// same reason: a read still finishing as this object goes away needs somewhere valid to
		// write, whether or not anything is still reading.
		struct Ready
		{
			std::mutex Mutex;
			std::vector<FileText> Texts;
		};

		std::shared_ptr<Ready> m_Ready = std::make_shared<Ready>();

		// Last, deliberately: members are destroyed in reverse order of declaration, so this one
		// joins its worker before anything the worker might still be touching goes away.
		Core::JobQueue m_Jobs{1};
	};
}

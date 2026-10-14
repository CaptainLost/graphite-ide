#pragma once

#include "Core/Base/Base.h"

#include "Sessions/SessionSummary.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace Graphite {

	using FingerprintsByFile = std::map<std::filesystem::path, Fingerprint>;

	std::vector<std::filesystem::path> SessionFilesUnder(const std::filesystem::path& projectDirectory);

	// WARNING: Synchronous on purpose, so tests can drive it; SessionIndex is the thread. Returns nothing when
	// the scan was cancelled, because it did not look everywhere.
	std::vector<std::filesystem::path> ScanProjects(const std::filesystem::path& projects,
													const std::vector<SessionSummary>& cached,
													const Core::CancellationToken& token,
													const std::function<void(SessionSummary)>& onSummary);

	std::optional<std::filesystem::path> SessionFileFor(const std::filesystem::path& projects,
														const std::filesystem::path& changedDirectory);

	struct IndexDelivery
	{
		std::vector<SessionSummary> Updated;
		std::vector<std::filesystem::path> Removed;
	};

	struct ScanProgress
	{
		bool Done = false;
		std::size_t Scanned = 0;
		std::size_t Total = 0;
	};

	class SessionIndex
	{
	public:
		~SessionIndex();

		SessionIndex() = default;
		SessionIndex(const SessionIndex&) = delete;
		SessionIndex& operator=(const SessionIndex&) = delete;

		void Start(std::filesystem::path projects, std::vector<SessionSummary> cached);

		void Refresh(std::vector<std::filesystem::path> changedDirectories);

		bool IsScanning() const;

		ScanProgress InitialScan() const;

		void Take(IndexDelivery& delivery);

	private:
		struct Inbox
		{
			std::mutex Mutex;
			IndexDelivery Ready;
			std::size_t Pending = 0;

			// WARNING: Read by the UI thread without the lock; the counts move once per session file.
			std::atomic<bool> InitialScanDone = false;
			std::atomic<std::size_t> Scanned = 0;
			std::atomic<std::size_t> Total = 0;
		};

		struct Known
		{
			FingerprintsByFile Fingerprints;
		};

		void Submit(std::function<void(Inbox&, Known&)> work);

		static void DeliverScannedSummary(Inbox& inbox, Known& known, SessionSummary summary);

		std::filesystem::path m_Projects;
		Core::Ref<Inbox> m_Inbox = Core::CreateRef<Inbox>();
		Core::Ref<Known> m_Known = Core::CreateRef<Known>();
		Core::CancellationToken m_Token;

		// WARNING: Keep last. It is destroyed first, so the worker is joined before what it touches.
		Core::JobQueue m_Jobs{1};
	};
}

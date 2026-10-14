#pragma once

#include "Core/Base/Base.h"

#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/SubagentLinks.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "Sessions/Transcript/TranscriptRecord.h"

#include "Core/Jobs/JobQueue.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	class LiveTranscript
	{
	public:
		explicit LiveTranscript(std::filesystem::path file);
		~LiveTranscript();

		LiveTranscript(const LiveTranscript&) = delete;
		LiveTranscript& operator=(const LiveTranscript&) = delete;

		void Poll();

		bool Take(Session& session);

		bool Take(Session& session, RequestTimeline& timeline);

		bool TakeSubagents(SubagentUsage& usage, SubagentLinks& links);

		struct LoadProgress
		{
			std::uint64_t Bytes = 0;
			std::uint64_t Total = 0;

			std::size_t SubagentsDone = 0;
			std::size_t SubagentsTotal = 0;

			bool SubagentsWalked = false;

			bool FirstPassDone = false;
		};

		LoadProgress GetProgress() const;

		std::size_t GetSkipped() const;
		const std::filesystem::path& GetFile() const { return m_File; }

	private:
		struct SubagentState
		{
			SubagentUsage Usage;
			SubagentLinks Links;
		};

		struct Inbox
		{
			std::mutex Mutex;
			std::vector<TranscriptRecord> Records;
			bool Restarted = false;
			std::size_t Skipped = 0;
			bool Reading = false;
			bool Again = false;
			bool Stopping = false;

			std::uint64_t Offset = 0;

			LoadProgress Progress;

			std::optional<SubagentState> Subagents;
		};

		struct SubagentFile
		{
			std::uint64_t Offset = 0;
			std::uintmax_t Size = 0;
			UsageTally Tally;

			std::optional<std::pair<std::string, SubagentInfo>> Link;
		};

		struct SubagentFiles
		{
			std::map<std::filesystem::path, SubagentFile> Files;
		};

		static bool ReadMain(const std::filesystem::path& file, Inbox& inbox);

		static std::optional<SubagentState> FollowSubagents(const std::filesystem::path& sessionFile,
															SubagentFiles& subagents, Inbox& inbox);

		bool TakeRecords(Session& session, RequestTimeline* timeline);

		std::filesystem::path m_File;
		Core::Ref<Inbox> m_Inbox = Core::CreateRef<Inbox>();
		Core::Ref<SubagentFiles> m_Subagents = Core::CreateRef<SubagentFiles>();

		// WARNING: Declared last so it is destroyed first: its worker is joined before the inbox
		// it writes into goes.
		Core::JobQueue m_Jobs{1};
	};
}

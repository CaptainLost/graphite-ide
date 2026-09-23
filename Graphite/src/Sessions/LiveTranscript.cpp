#include "Sessions/LiveTranscript.h"

#include "Sessions/TranscriptReader.h"

#include <utility>

namespace Graphite {

	LiveTranscript::LiveTranscript(std::filesystem::path file) : m_File(std::move(file)) {}

	LiveTranscript::~LiveTranscript()
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		m_Inbox->Stopping = true;
	}

	void LiveTranscript::Poll()
	{
		{
			const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

			if (m_Inbox->Reading)
			{
				m_Inbox->Again = true;
				return;
			}

			m_Inbox->Reading = true;
		}

		m_Jobs.Submit([inbox = m_Inbox, file = m_File]
		{
			while (true)
			{
				std::uint64_t offset = 0;

				{
					const std::lock_guard<std::mutex> guard(inbox->Mutex);

					if (inbox->Stopping)
					{
						inbox->Reading = false;
						return;
					}

					inbox->Again = false;
					offset = inbox->Offset;
				}

				// A missing or locked file leaves the offset where it was; the next poll retries.
				Core::Result<TranscriptRead> read = ReadTranscript(file, offset);

				const std::lock_guard<std::mutex> guard(inbox->Mutex);

				if (read.HasValue())
				{
					TranscriptRead& value = read.GetValue();

					if (value.Restarted)
					{
						inbox->Records.clear();
						inbox->Restarted = true;
						inbox->Skipped = 0;
					}

					for (TranscriptRecord& record : value.Chunk.Records)
						inbox->Records.push_back(std::move(record));

					inbox->Skipped += value.Chunk.Skipped;
					inbox->Offset = value.NextOffset;
				}

				if (!inbox->Again)
				{
					inbox->Reading = false;
					return;
				}
			}
		});
	}

	bool LiveTranscript::Take(Session& session)
	{
		std::vector<TranscriptRecord> records;
		bool restarted = false;

		{
			const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
			records.swap(m_Inbox->Records);
			restarted = std::exchange(m_Inbox->Restarted, false);
		}

		if (restarted)
			session.Reset();

		session.Append(records);
		return restarted || !records.empty();
	}

	std::size_t LiveTranscript::GetSkipped() const
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		return m_Inbox->Skipped;
	}
}

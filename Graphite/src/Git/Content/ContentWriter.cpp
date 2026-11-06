#include "Git/Content/ContentWriter.h"

#include "Git/Content/ContentOrder.h"
#include "Git/Content/PatchIdFile.h"

#include "Core/Base/Log.h"

#include <functional>
#include <map>

namespace Graphite {

	namespace {

		void KeepFoundOf(const LineRead& read, const Core::BranchLine& line, const ContentCache& cache, ContentCache& kept)
		{
			if (line.Ref >= read.Refs.size())
			{
				return;
			}

			const std::map<Core::Oid, ContentFound>::const_iterator found = cache.Found.find(read.Refs[line.Ref].Tip);

			if (found != cache.Found.end())
			{
				kept.Found.insert(*found);
			}
		}

		void KeepVerdictOf(const LineRead& read, const Core::BranchLine& line, const ContentCache& cache, ContentCache& kept)
		{
			const std::optional<ContentKey> key = ContentKeyOf(read, line);

			if (!IsContentCandidate(line) || !key.has_value())
			{
				return;
			}

			const std::map<ContentKey, Core::ContentVerdict>::const_iterator verdict = cache.Verdicts.find(*key);

			if (verdict != cache.Verdicts.end())
			{
				kept.Verdicts.insert(*verdict);
			}
		}
	}

	void LoadPatchIdsOnce(PatchIdStore& store)
	{
		std::call_once(store.Loaded, LoadPatchIds, std::cref(store.File), std::ref(store.Ids));
	}

	ContentCache VerdictsUsedBy(const LineRead& read, const ContentCache& cache)
	{
		ContentCache kept;

		for (const Core::BranchLine& line : read.Model.Branches)
		{
			KeepFoundOf(read, line, cache, kept);
			KeepVerdictOf(read, line, cache, kept);
		}

		return kept;
	}

	bool ContentWriter::IsNewest(const std::filesystem::path& file, std::uint64_t sequence)
	{
		std::uint64_t& written = m_Written[file];

		if (sequence <= written)
		{
			return false;
		}

		written = sequence;
		return true;
	}

	void ContentWriter::Write(const ContentWrite& write)
	{
		const std::lock_guard<std::mutex> lock{m_Mutex};

		if (write.Verdicts.has_value() && IsNewest(write.VerdictsFile, write.Sequence))
		{
			if (const Core::Result<void> saved = SaveContentCache(write.VerdictsFile, *write.Verdicts); !saved.HasValue())
			{
				APP_WARN("Could not save the content checks: {}", saved.GetError().Message);
			}
		}

		if (write.PatchIds != nullptr && IsNewest(write.PatchIds->File, write.Sequence))
		{
			// Read first, so the file is written over with what it held as well.
			LoadPatchIdsOnce(*write.PatchIds);

			if (const Core::Result<void> saved = SavePatchIds(write.PatchIds->File, write.PatchIds->Ids); !saved.HasValue())
			{
				APP_WARN("Could not save the patch ids: {}", saved.GetError().Message);
			}
		}
	}
}

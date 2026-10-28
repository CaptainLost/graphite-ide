#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/Result.h"
#include "Core/Git/Blame.h"
#include "Core/Git/LineHistory.h"

#include <filesystem>
#include <string>

namespace Graphite {

	struct HeadBlame
	{
		bool Tracked = false;    // false: untracked, ignored, outside, submodule, no repository, unborn HEAD, path not in HEAD
		bool Unusable = false;   // binary or over Core::MaximumDiffTextSize
		Core::Ref<const Core::Blame> Blame;
		std::string HeadText;    // decoded with DecodeBaseText (LF line ends)
	};

	class BlamePort
	{
	public:
		virtual ~BlamePort() = default;

		// WARNING: Called on a worker thread.
		virtual Core::Result<HeadBlame> BlameAtHead(const std::filesystem::path& absolutePath) = 0;
		virtual Core::Result<Core::LineHistory> QueryLineHistory(const Core::LineHistoryRequest& request) = 0;
	};
}

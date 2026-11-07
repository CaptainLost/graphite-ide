#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Oid.h"
#include "Core/Git/PatchMatch.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// Whether the open folder's repository holds a branch's changes on its line without holding its commits, asked on a
	// handle that writes no object into the repository.
	class ContentPort
	{
	public:
		virtual ~ContentPort() = default;

		// The number LinesPort::RepositoryNow gives: which repository the port opens now. Asked on the UI thread when a
		// check is asked.
		virtual std::uint64_t RepositoryNow() = 0;

		// WARNING: Checks nothing, and fails with Cancelled, once the port no longer opens `repository`: an answer about
		// the folder before must not be taken for one about the next folder's commits.
		virtual Core::Result<Core::ContentVerdict> CheckContent(std::uint64_t repository, const Core::Oid& branchTip,
																const Core::Oid& lineTip, Core::PatchIdCache& patchIds) = 0;

		// The commit lineTip reaches that carries `commit`'s patch, among the first `budget` line commits read newest
		// first (Repository::FindPatchTwin); NotFound when a commit or an object a patch needs is missing. Refused like
		// CheckContent.
		virtual Core::Result<Core::PatchTwinSearch> FindTwin(std::uint64_t repository, const Core::Oid& commit,
															 const Core::Oid& lineTip, std::size_t budget,
															 Core::PatchIdCache& patchIds) = 0;

		// The paths at which two commits' trees differ, in order (Repository::PathsChangedBetween): from trees alone, so a
		// blobless clone answers. Refused like CheckContent.
		virtual Core::Result<std::vector<std::string>> PathsChangedBetween(std::uint64_t repository, const Core::Oid& older,
																		   const Core::Oid& newer) = 0;

		// Whether two commits' trees differ at any of `paths`, taken as written (Repository::TreesDifferAt). Refused like
		// CheckContent.
		virtual Core::Result<bool> TreesDifferAt(std::uint64_t repository, const Core::Oid& from, const Core::Oid& to,
												 const std::vector<std::string>& paths) = 0;
	};
}

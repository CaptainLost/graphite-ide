#include "Git/Lines/HeadInRead.h"

#include "Core/Git/Ref.h"

#include <string>

namespace Graphite {

	HeadInRead PlaceHeadInRead(const HeadInReadInput& input)
	{
		// Nothing read of HEAD contradicts the read.
		if (input.Head == nullptr)
		{
			return HeadInRead::Agrees;
		}

		// An unborn branch has no ref yet, so a read lists it as no branch checked out, as it does a detached HEAD.
		const std::string headBranch =
			input.Head->Kind == Core::HeadKind::Branch ? Core::LocalBranchReference(input.Head->BranchName) : std::string{};

		if (headBranch == input.ReadCheckedOut)
		{
			return HeadInRead::Agrees;
		}

		return input.ReadComing ? HeadInRead::WaitsForRead : HeadInRead::Differs;
	}
}

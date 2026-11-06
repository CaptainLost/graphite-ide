#pragma once

namespace Core {

	// What a content question is answered with. Unknown is an answer, not a failure: the objects
	// the question needs are not in the repository (a blobless or shallow clone), so neither Yes
	// nor No can be said.
	enum class ContentAnswer
	{
		Yes,
		No,
		Unknown,
	};
}

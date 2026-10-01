#pragma once

namespace Graphite {

	class GitModel;

	// What the Changes and Git panels show in a folder that is not a repository: a sentence
	// and an Initialize Repository button that queues the init at once, with no
	// confirmation - git init deletes and overwrites nothing. Greyed while any write is in
	// flight. A failed init's message is drawn under the sentence, inside the prompt,
	// because the prompt claims the panel's whole remaining area.
	void DrawInitRepositoryPrompt(GitModel& model);
}

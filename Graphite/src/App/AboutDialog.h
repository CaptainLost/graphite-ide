#pragma once

#include "UI/ModalGate.h"
#include "UI/ModalQuestion.h"

#include <string>

namespace Graphite {

	// The About window: what the application is, which version, and the licences of what it is
	// built from. A modal like the other questions, so it takes the same gate; Escape and the
	// Close button both dismiss it.
	class AboutDialog
	{
	public:
		explicit AboutDialog(ModalGate& gate);

		void Open();

		// Outside every window, as the other modals are.
		void Render();

		// The notices file the build copies next to the executable.
		static constexpr const char* kNoticesFile = "THIRD_PARTY_NOTICES.txt";

		// The text of the licences child: the file's contents, or a sentence saying it could
		// not be read, so a missing file leaves the dialog working.
		static std::string LoadNotices();

	private:
		struct Request
		{
			bool operator==(const Request&) const = default;
		};

		static constexpr const char* kTitle = "About Graphite##AboutGraphite";

		ModalQuestion<Request> m_Question;

		// Read when the dialog opens rather than every frame: the file is about 85 KB.
		std::string m_Notices;
	};
}

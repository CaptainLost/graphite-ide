#pragma once

#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/ModalQuestion.h"

#include <string>

namespace Graphite {

	class AboutDialog
	{
	public:
		explicit AboutDialog(ModalGate& gate);

		void Open();

		void Render();

		static constexpr const char* kNoticesFile = "THIRD_PARTY_NOTICES.txt";

		static std::string LoadNotices();

	private:
		struct Request
		{
			bool operator==(const Request&) const = default;
		};

		static constexpr const char* kTitle = "About Graphite##AboutGraphite";

		ModalQuestion<Request> m_Question;

		// WARNING: Loaded once in Open, never per frame: the notices file is about 85 KB.
		std::string m_Notices;
	};
}

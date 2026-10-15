#include "App/AboutDialog.h"

#include "App/Version.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/Process.h"
#include "UI/Dialogs/DialogLayout.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr float kLicencesHeight = 260.0f;
		constexpr float kWrapAtWindowEdge = 0.0f;
	}

	AboutDialog::AboutDialog(ModalGate& gate)
		: m_Question(gate, kTitle)
	{
	}

	void AboutDialog::Open()
	{
		if (m_Question.IsAsked())
		{
			return;
		}

		m_Notices = LoadNotices();
		m_Question.Ask(Request{});
	}

	std::string AboutDialog::LoadNotices()
	{
		const Core::Result<std::filesystem::path> directory = Core::ExecutableDirectory();

		if (!directory)
		{
			return "The third-party licences could not be found: the application's folder is unknown.";
		}

		Core::Result<std::string> text = Core::ReadFileBytes(directory.GetValue() / kNoticesFile);

		if (!text)
		{
			return std::string("The third-party licences could not be read from ") + kNoticesFile +
				   " next to the application.";
		}

		return std::move(text).GetValue();
	}

	void AboutDialog::Render()
	{
		DialogLayout::BeforeBegin(DialogLayout::kWideWidth);
		const bool isOpen = m_Question.Begin();
		DialogLayout::AfterBegin();

		if (!isOpen)
		{
			return;
		}

		ImGui::TextUnformatted("Graphite");
		ImGui::TextDisabled("Version %s", Version::String());
		DialogLayout::Gap();
		DialogLayout::Text("A code editor built on Dear ImGui.");
		DialogLayout::Gap();
		ImGui::TextUnformatted("Third-party licences");

		if (ImGui::BeginChild("##Licences", ImVec2(0.0f, Core::Scaled(kLicencesHeight)), ImGuiChildFlags_Borders))
		{
			ImGui::PushTextWrapPos(kWrapAtWindowEdge);
			ImGui::TextUnformatted(m_Notices.c_str(), m_Notices.c_str() + m_Notices.size());
			ImGui::PopTextWrapPos();
		}

		ImGui::EndChild();
		DialogLayout::Gap();

		if (DialogLayout::AcknowledgeRow("Close"))
		{
			m_Question.Answer();
		}

		m_Question.End();
	}
}

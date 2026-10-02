#include "corepch.h"

#include "Core/ImGui/TextInput.h"

namespace Core {

	namespace {
		// Called by ImGui when the text outgrows the buffer it was handed: the string grows to
		// fit and ImGui carries on in its new storage - the same callback imgui_stdlib uses.
		int Resize(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
			{
				auto* text = static_cast<std::string*>(data->UserData);
				IM_ASSERT(data->Buf == text->data());

				text->resize(static_cast<std::size_t>(data->BufTextLen));
				data->Buf = text->data();
			}

			return 0;
		}
	}

	bool InputText(const char* label, std::string& text, ImGuiInputTextFlags flags)
	{
		return ImGui::InputText(label, text.data(), text.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
								Resize, &text);
	}

	bool InputTextWithHint(const char* label, const char* hint, std::string& text, ImGuiInputTextFlags flags)
	{
		return ImGui::InputTextWithHint(label, hint, text.data(), text.capacity() + 1,
										flags | ImGuiInputTextFlags_CallbackResize, Resize, &text);
	}

	bool InputTextMultiline(const char* label, std::string& text, const ImVec2& size, ImGuiInputTextFlags flags)
	{
		return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, size,
										 flags | ImGuiInputTextFlags_CallbackResize, Resize, &text);
	}
}

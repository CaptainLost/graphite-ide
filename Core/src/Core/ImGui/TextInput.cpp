#include "corepch.h"

#include "Core/ImGui/TextInput.h"

namespace Core {

	namespace {
		// WARNING: ImGui keeps writing into the callback data's Buf, so it must point at the string's new storage after the resize.
		int Resize(ImGuiInputTextCallbackData* callbackData)
		{
			if (callbackData->EventFlag == ImGuiInputTextFlags_CallbackResize)
			{
				auto* text = static_cast<std::string*>(callbackData->UserData);
				IM_ASSERT(callbackData->Buf == text->data());

				text->resize(static_cast<std::size_t>(callbackData->BufTextLen));
				callbackData->Buf = text->data();
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

#pragma once

#include "Core/Base/Color.h"
#include "Core/ImGui/ToastStack.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Core {

	struct ToastPalette
	{
		Color Surface;
		Color Text;
		Color TextDim;

		Color Success;
		Color Warning;
		Color Error;
	};

	class Toasts
	{
	public:
		std::uint64_t Show(ToastKind kind, std::string title, std::string message, std::string copyText = {},
						   std::uint32_t group = 0);

		void Render(const ToastPalette& palette);

		void Clear() { m_Stack.Clear(); }

		void Clear(std::uint32_t group) { m_Stack.Clear(group); }

		std::vector<ToastDeparture> TakeDepartures() { return m_Stack.TakeDepartures(); }

		std::function<void(std::string_view)> OnCopied;

		const ToastStack& GetStack() const { return m_Stack; }

	private:
		ToastStack m_Stack;

		// An ImGuiID, kept as its integer so this header needs no ImGui.
		std::uint32_t m_FocusBeforeToasts = 0;

		std::unordered_set<std::uint64_t> m_Drawn;
	};
}

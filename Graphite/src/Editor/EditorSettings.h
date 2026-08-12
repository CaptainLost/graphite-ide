#pragma once

#include "Core/Text/Editing.h"

#include <cstddef>

namespace Core {

	class CommandRegistry;
	class Keymap;
}
namespace Graphite {

	class EditorSettings final
	{
	public:
		void Load();

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		float GetFontSize() const { return m_FontSize; }

		void SetFontSize(float size);

		Core::IndentStyle GetIndentStyle() const
		{
			return {m_IndentWithSpaces, m_IndentWidth};
		}

		bool IsWordWrap() const { return m_WordWrap; }

	private:
		void SetWordWrap(bool wrap);

		static constexpr float kMinimumFontSize = 8.0f;
		static constexpr float kMaximumFontSize = 32.0f;
		static constexpr float kDefaultFontSize = 13.0f;

		static constexpr float kFontSizeStep = 1.0f;

		float m_FontSize = kDefaultFontSize;

		bool m_IndentWithSpaces = true;
		std::size_t m_IndentWidth = 4;

		bool m_WordWrap = false;
	};
}
#pragma once

#include "Core/Base/KeyCodes.h"

#include <vector>

namespace Core {

	// The modifiers travel with the stroke rather than being polled when it is read: a queue
	// is drained after the fact, and by then the keyboard may be holding something else.
	struct KeyStroke
	{
		KeyCode Key = KeyCode::Space;
		KeyModifier Modifiers = KeyModifier::None;
		bool IsRepeat = false;
	};

	// Holds the keyboard input nobody upstream consumed until something drawing this frame can
	// say it was meant for it: events are dispatched before any window has been submitted, so a
	// widget only learns it has the keyboard while drawing. Clear it at the end of a frame,
	// never the start, which would drop everything that arrived while the previous frame was
	// being built.
	class FrameInput
	{
	public:
		void PushKey(KeyCode key, KeyModifier modifiers, bool isRepeat)
		{
			m_Strokes.push_back(KeyStroke{.Key = key, .Modifiers = modifiers, .IsRepeat = isRepeat});
		}

		void PushCharacter(unsigned int codepoint) { m_Characters.push_back(codepoint); }

		void Clear()
		{
			m_Strokes.clear();
			m_Characters.clear();
		}

		const std::vector<KeyStroke>& GetStrokes() const { return m_Strokes; }
		const std::vector<unsigned int>& GetCharacters() const { return m_Characters; }

	private:
		std::vector<KeyStroke> m_Strokes;
		std::vector<unsigned int> m_Characters;
	};
}

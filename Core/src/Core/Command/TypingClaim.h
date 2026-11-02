#pragma once

#include "Core/Command/ChordFlags.h"

namespace Core {

	// What held the keyboard on the frame drawn last, as the widget holding it said while it drew. A stroke arrives
	// between two frames, so this is what it was aimed at.
	class TypingClaim final
	{
	public:
		// WARNING: Released as every frame begins, so a widget that stops drawing stops holding the keyboard.
		void Release() { m_Target = TypingTarget::Nothing; }

		void Claim(TypingTarget target) { m_Target = target; }

		TypingTarget GetTarget() const { return m_Target; }

	private:
		TypingTarget m_Target = TypingTarget::Nothing;
	};
}

#pragma once

#include "Core/Base/Timestep.h"

namespace Graphite {

	class Debouncer
	{
	public:
		static constexpr float kQuietSeconds = 0.15f;

		// WARNING: The cap counts from the first mark of a burst, not the latest. A build
		// writing into an output directory keeps marking and would starve the refresh without it.
		static constexpr float kMaxSeconds = 1.0f;

		void Mark();

		bool Advance(Core::Timestep step);

		void Reset();

		bool IsDirty() const { return m_Dirty; }

	private:
		// WARNING: A mark can come from a result that woke the loop for a few frames only, so
		// the loop would sleep through the debounce until its idle tick without this request.
		void RequestFrame() const;

		bool m_Dirty = false;
		float m_DirtyFor = 0.0f;
		float m_DirtySince = 0.0f;
	};
}

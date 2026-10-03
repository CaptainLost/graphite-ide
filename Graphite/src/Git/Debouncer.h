#pragma once

#include "Core/Base/Timestep.h"

namespace Graphite {

	// Coalesces a burst of "something changed" marks into one refresh.
	//
	// A save touches several files, and refreshing per file would run the same query five
	// times for one action. The quiet timer restarts on every mark; the cap does not, so a
	// sustained stream of marks cannot postpone the refresh for as long as it lasts.
	//
	// UI thread only.
	class Debouncer
	{
	public:
		// How long a burst may keep coalescing after its most recent mark.
		static constexpr float kQuietSeconds = 0.15f;

		// A ceiling on how long coalescing can postpone the refresh, measured from the first
		// mark of the burst rather than the most recent one. Without it a build writing into
		// an output directory, which Workspace::Update republishes per settled batch, would
		// keep resetting the quiet timer and starve the refresh.
		static constexpr float kMaxSeconds = 1.0f;

		// The burst's start is reset only when it was not already dirty: a mark that arrives
		// mid-burst extends the quiet timer but must not push the cap out. Also asks for the
		// frame the debounce runs out on.
		void Mark();

		// Adds the elapsed time, and returns true exactly once per burst - when it has run out
		// of quiet or hit the cap - after which the debouncer is clean again. Then asks the
		// frame loop for the frame the remaining debounce runs out on.
		bool Advance(Core::Timestep step);

		// Forgets any pending burst, without firing it.
		void Reset();

		bool IsDirty() const { return m_Dirty; }

	private:
		// A mark can come from a result that woke the loop for a few frames only, and the
		// loop would otherwise sleep through the debounce until its idle tick.
		void RequestFrame() const;

		bool m_Dirty = false;
		float m_DirtyFor = 0.0f;
		float m_DirtySince = 0.0f;
	};
}

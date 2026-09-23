#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"

#include <optional>

namespace Graphite {

	void DrawCost(const Session& session, const std::optional<SessionSummary>& summary, const Pricing& pricing);
}

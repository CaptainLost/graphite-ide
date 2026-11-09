#pragma once

namespace Agent {

	// Points Core's loggers at stderr alone. Core logs to stdout by default, and the agent's
	// stdout is the frame stream Graphite reads (ADR 0695).
	void SendLogsToStandardError();
}

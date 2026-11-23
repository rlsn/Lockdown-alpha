#pragma once

#include "ProfilingDebugging/CpuProfilerTrace.h"

// Times one expression as a named Unreal Insights scope and yields its value, so each part of a chained condition is
// attributed on its own. Use it for work that runs a few times per authority step, never once per body.
#define LD_TRACED(Name, Expression) ([&]() { TRACE_CPUPROFILER_EVENT_SCOPE(Name); return (Expression); }())

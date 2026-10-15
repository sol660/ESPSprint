#pragma once

// Tagged reply lines for installer commands (ADR 0001): `@tag key=value ...`,
// one line, no newline. Board tests parse these; keys are only ever added.

#include <cstddef>

#include "WakeCycle.h"

// `@read ok= pitch= roll= tilt= stable= noise= temp= g=` for the `read` command.
void formatReadReply(const CommandResult& r, char* out, size_t size);

// `@status ok= baseline= [bpitch= broll=] pitch= roll= tilt= stable= noise= temp= g=`
// for `status` and `baseline`. bpitch/broll appear only when there is a baseline.
void formatStatusReply(const CommandResult& r, char* out, size_t size);

// `@boot fw=<ver> boot=<n> cause=<PowerOn|Timer|...> seq=<n>`, the first line of every boot.
void formatBootLine(const BootInfo& b, char* out, size_t size);

// `@sleep s=<seconds>`, printed just before deep sleep with the timer wake set.
void formatSleepLine(uint32_t timerS, char* out, size_t size);

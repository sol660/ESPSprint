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

// `@upload ok=<0|1> tag=<n|none> seq=<n> boot=<n>` after each upload attempt.
// ok=0 means the reading was dropped (no buffer in the first build).
void formatUploadLine(const Reading& r, bool ok, char* out, size_t size);

// `@sleep s=<seconds>`, printed just before deep sleep with the timer wake set.
void formatSleepLine(uint32_t timerS, char* out, size_t size);

// `@alert ok=<0|1> tag=<n|none> level=<OK|WATCH|ALERT> tilt=<deg>` after each
// notification attempt: the `alert` command (tag = its number) and wake alerts
// (tag=none). ok=0: not delivered (a wake alert stays in flash for the next wake).
void formatAlertLine(const Alert& a, bool ok, char* out, size_t size);

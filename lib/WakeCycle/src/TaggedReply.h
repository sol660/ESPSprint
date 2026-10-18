#pragma once

// Tagged reply lines for installer commands (ADR 0001): `@tag key=value ...`,
// one line, no newline. Board tests parse these; keys are only ever added.

#include <cstddef>

#include "WakeCycle.h"

// `@read ok= pitch= roll= tilt= stable= noise= temp= g= cal=` for the `read` command.
// cal=1: the reading was corrected with the stored accelerometer calibration (#22).
void formatReadReply(const CommandResult& r, char* out, size_t size);

// `@status ok= baseline= [bpitch= broll=] pitch= roll= tilt= stable= noise= temp= g= cal=`
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

// `@cal ok=<0|1> step=<+x|...|-z|save|clear> faces=<n> ...` for the `cal` command (#22):
//  - a face: `ax= ay= az=` (the raw average, g) when one was measured, also when refused;
//  - save: `ox= oy= oz= sx= sy= sz=` once worked out, also when refused as out of range;
//  - refused: `err=<moving|wrong_face|missing_faces|out_of_range|flash>`, and for
//    missing_faces `missing=<face>,<face>...`;
//  - `baseline=0` when the baseline was erased (save and clear).
// faces= is the number captured after the step (0 after a save or clear).
void formatCalReply(const CalResult& r, char* out, size_t size);

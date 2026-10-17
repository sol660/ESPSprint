// How long ago the newest reading was, for the status card and OFFLINE badge.
import { test } from "node:test";
import assert from "node:assert/strict";
import { formatAge } from "../js/format.js";

test("under a minute", () => assert.equal(formatAge(0.4), "just now"));
test("minutes", () => assert.equal(formatAge(42.6), "42 min"));
test("hours and minutes", () => assert.equal(formatAge(190), "3 h 10 min"));
test("whole hours", () => assert.equal(formatAge(120), "2 h"));
test("days and hours", () => assert.equal(formatAge(60 * 50), "2 d 2 h"));
test("a reading slightly in the future (clock skew) counts as just now", () => assert.equal(formatAge(-3), "just now"));

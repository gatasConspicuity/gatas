import test from "node:test";
import assert from "node:assert/strict";

import { formatUnit } from "../src/components/units.js";

test("formatUnit formats boolean unit b as checkmark/disabled symbols", () => {
  assert.equal(formatUnit(1, "b"), "&#10003;");
  assert.equal(formatUnit(0, "b"), "&#x292B;");
});

test("formatUnit formats binary unit bin as dots", () => {
  assert.equal(formatUnit(5, "bin"), "·····●·●");
});

test("formatUnit formats frequency using UNIT_TABLE thresholds", () => {
  assert.equal(formatUnit(1_000_000, "hz", "en-US"), "1.000MHz");
  assert.equal(formatUnit(1_500, "hz", "en-US"), "1.5kHz");
  assert.equal(formatUnit(150, "hz", "en-US"), "150Hz");
});

test("formatUnit formats meters unit m as m below 1000 and Km above", () => {
  assert.equal(formatUnit(999, "m", "en-US"), "999m");
  assert.equal(formatUnit(1_500, "m", "en-US"), "1.5Km");
  assert.equal(formatUnit(55_101, "m", "en-US"), "55.1Km");
});

test("formatUnit converts negative values while preserving the sign", () => {
  assert.equal(formatUnit(-32.9, "ft", "en-US"), "-108ft");
});

test("formatUnit formats elapsed seconds with unit el", () => {
  assert.equal(formatUnit(3661, "el"), "1hour 1min 1sec");
  assert.equal(formatUnit(0, "el"), "0secs");
});

test("formatUnit formats protocol timing stats with unit dts", () => {
  const value = "00000000001111111111";
  const expected = "0..500ms\n··········|●●●●●●●●●●";
  assert.equal(formatUnit(value, "dts"), expected);
});

test("formatUnit returns original value when unit is unknown", () => {
  assert.equal(formatUnit(42, "unknown"), 42);
});

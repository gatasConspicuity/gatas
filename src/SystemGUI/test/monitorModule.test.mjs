import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { formatUnit2 } from "../src/components/units.js";

let MonitorModule;
const source = readFileSync(new URL("../src/components/monitormodule.js", import.meta.url), "utf8")
  .replace(/^import .*;$/gm, "");
vm.runInNewContext(source, {
  El: class {},
  Path2D: class {},
  customElements: { define: (_name, element) => { MonitorModule = element; } },
  formatUnit2: (key, value) => formatUnit2(key, value, "en-US"),
});
const html = (parts, ...values) => parts.reduce((text, part, index) =>
  text + part + (values[index] === undefined ? "" : [values[index]].flat(Infinity).join("")), "");

test("Client diagnostics format nested byte counters and subscriptions", () => {
  const monitor = new MonitorModule();
  const output = monitor._renderStructuredValue(html, [{
    peerAddress: "00:00:00:00:00:01",
    "cobsBytesSent:kb": 1500,
    "cobsSubscribed:b": 1,
    "nmeaSubscribed:b": 0,
  }]);
  assert.match(output, /00:00:00:00:00:01/);
  assert.match(output, /1\.5KB/);
  assert.match(output, /&#10003;/);
  assert.match(output, /&#x292B;/);
  assert.doesNotMatch(output, /cobsBytesSent:kb|cobsSubscribed:b/);
});

test("Monitor explains Bluetooth totals and the UDP recipient distinction", () => {
  const monitor = new MonitorModule();
  monitor.state = { data: [] };
  monitor.selected = "Bluetooth";
  assert.match(monitor.render(html), /current connection; totals cover all connections since boot/);
  monitor.selected = "GatasConnect";
  assert.match(monitor.render(html), /UDP does not confirm reception/);
});

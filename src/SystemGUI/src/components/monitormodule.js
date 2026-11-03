import { El } from "@frameable/el";
import store from "./store";
import "./moduleconfigs";
import { isDarkMode } from "./utils";
import { formatUnit, formatUnit2, bitsToDots } from "./units";

const MAX_DISTANCE_IDX = 3;
const AVG_DISTANCE_IDX = 2;
const PLANE_PATH = new Path2D('m 15.388371,4.7812376 c 0.06737,0.067371 0.06088,0.1535326 -0.171754,0.656096 -0.02746,0.059318 -0.18034,0.2765235 -0.18034,0.2765235 -10e-7,-1e-6 0.102687,0.1129918 0.130532,0.1408372 0.05383,0.053834 0.07864,0.1746392 0.05668,0.2696526 -0.06814,0.2947833 -0.8899,1.4704243 -1.349979,1.9305048 -0.285512,0.2855112 -0.432705,0.4805551 -0.422513,0.5599149 0.0086,0.06697 0.116774,0.3550941 0.240455,0.6389223 0.218228,0.5008044 0.299971,0.5993204 2.76179,3.3388821 1.949531,2.169479 2.546055,2.86956 2.58145,3.028007 0.09814,0.43933 -0.282015,0.847468 -1.264103,1.35685 l -0.506673,0.262782 c 0,0 -7.3888187,-5.289995 -7.3888187,-5.289995 l -4.429513,3.364643 0.101334,0.18034 c 0.055981,0.09885 0.518862,0.676109 1.028802,1.282996 0.509938,0.606889 0.932924,1.160835 0.939489,1.231471 0.026392,0.283961 -1.110644,1.177107 -1.281278,1.006472 -0.269318,-0.269317 -1.398977,-1.131169 -1.494252,-1.14044 -0.068024,-0.0066 -1.039054,-0.747073 -1.368872,-1.076892 -0.329819,-0.329818 -1.070274,-1.300849 -1.076892,-1.368872 -0.00927,-0.09528 -0.871124,-1.224934 -1.140441,-1.494253 -0.17063503,-0.170635 0.722512,-1.30767 1.006474,-1.281277 0.070635,0.0066 0.624579,0.42955 1.231469,0.939488 0.606887,0.50994 1.184148,0.972821 1.282997,1.028802 l 0.180339,0.101334 3.364644,-4.429513 c 0,0 -5.289996,-7.3888202 -5.289997,-7.3888202 l 0.262784,-0.506672 c 0.509382,-0.9820888 0.917519,-1.3622407 1.356849,-1.2641029 0.158448,0.035395 0.858528,0.6319166 3.028007,2.5814488 2.7395627,2.4618182 2.8380777,2.5435617 3.3388797,2.7617902 0.283829,0.1236801 0.571952,0.2318519 0.638923,0.2404552 0.07936,0.010191 0.274402,-0.1370033 0.559915,-0.4225135 0.460079,-0.4600805 1.635721,-1.2818435 1.930505,-1.3499793 0.09501,-0.021963 0.215817,0.00284 0.269652,0.056678 0.02785,0.027846 0.139121,0.1288154 0.139121,0.1288154 0,0 0.217204,-0.1528832 0.276522,-0.1803404 0.502564,-0.2326341 0.590442,-0.2374085 0.657815,-0.1700356 z')

const POLAR_COLORS = {
  DARK: {
    grid: "#666",
    legend: "#999",
    planeFill: "#999",
    avgDistance: "rgb(241, 120, 7)",
    maxDistance: "rgba(23, 208, 23, 1)",
  },
  LIGHT: {
    grid: "#CCC",
    legend: "#333",
    planeFill: "#333",
    avgDistance: "rgb(241, 120, 7)",
    maxDistance: "rgba(23, 208, 23, 1)",
  }
}


class MonitorModule extends El {

  created() {
    this.state = this.$observable({ data: [] });
  }

  mounted() {
    this._running = true;
    this._colorSchema = this._getPolarColorSchema();
    this._fetchData();
  }

  unmounted() {
    this._running = false;
    clearTimeout(this.timer);
  }

  _fetchData() {
    store
      .fetch(`/api/${this.selected}.json`)
      .then((data) => {
        this.state.data.length = 0;
        for (var prop in data) {
          this.state.data.push({ name: prop, value: data[prop] });
        }

        // render polar plots
        for (var prop in data) {
          if (prop.endsWith("AntPolar") && Array.isArray(data[prop])) {
            this._drawPolar(`polar-${prop}`, data[prop]);
          }
        }

      })
      .catch((e) => {
        this.state.data.length = 0;
      })
      .finally(() => {
        if (this._running) {
          this.timer = this.timer = setTimeout(() => {
            this._fetchData();
          }, 750);
        }
      });
  }

  _getPolarColorSchema() {
    return POLAR_COLORS[isDarkMode() ? "DARK" : "LIGHT"];
  }

  _isCompactTimeline() {
    return typeof window !== "undefined" && window.matchMedia && window.matchMedia("(max-width: 640px)").matches;
  }

  /**
   * Draw a small 2D plane
   * 
   * @param {*} ctx 
   * @param {*} cx 
   * @param {*} cy 
   */
  _plane(ctx, cx, cy) {
    const WX = 10;
    const WY = 10;
    const SCALE = 1.5;

    ctx.save();
    ctx.lineWidth = 1;
    ctx.fillStyle = this._colorSchema.planeFill;
    ctx.translate(cx, cy);
    ctx.rotate(-Math.PI / 4); // nose pointing north
    ctx.translate(-WY * SCALE, -WY * SCALE);
    ctx.scale(SCALE, SCALE);
    ctx.fill(PLANE_PATH);
    ctx.restore();
  }

  /**
   * Draw the spokes
   * @param {*} ctx 
   * @param {*} cx 
   * @param {*} cy 
   * @param {*} rMax 
   * @param {*} n 
   * @param {*} legend 
   */
  _drawGrid(ctx, cx, cy, rMax, n, legend) {
    ctx.save();
    ctx.strokeStyle = this._colorSchema.grid;
    ctx.lineWidth = 1;

    // Arcs
    const nCircles = 2;
    for (let i = 1; i <= nCircles; i++) {
      ctx.beginPath();
      ctx.arc(cx, cy, rMax * i / nCircles, 0, Math.PI * 2);
      ctx.stroke();
    }

    // Spokes
    const offsetRot = (2 * Math.PI) / n / 2;
    for (let i = 0; i < n; i++) {
      const a = (i / n) * Math.PI * 2 - Math.PI / 2 + offsetRot;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(
        cx + Math.cos(a) * rMax,
        cy + Math.sin(a) * rMax
      );
      ctx.stroke();
    }

    // ---- label at top of outer circle ----
    ctx.fillStyle = this._colorSchema.legend;
    ctx.font = "12px sans-serif";
    ctx.textAlign = "center";
    ctx.textBaseline = "bottom";
    ctx.fillText(legend, cx, 12);
    ctx.restore();
  }

  /**
   * Draw the polar diagram
   * @param {*} ctx 
   * @param {*} data 
   * @param {*} index 
   * @param {*} lineWidth 
   * @param {*} color 
   * @param {*} maxPolar 
   */
  _drawDatapoints(ctx, data, index, lineWidth, color, maxPolar) {
    ctx.save();
    const w = ctx.canvas.width;
    const h = ctx.canvas.height;
    const cx = w / 2;
    const cy = h / 2;

    // Maximum radius
    const rMax = Math.min(cx, cy) - 4;

    const n = data.length;
    const points = [];
    for (let i = 0; i < n; i++) {
      const value = data[i][index];
      const a = (i / n) * Math.PI * 2 - Math.PI / 2;
      const r = (value / maxPolar) * rMax;
      const x = cx + Math.cos(a) * r;
      const y = cy + Math.sin(a) * r;
      points.push([x, y]);
    }

    ctx.strokeStyle = color;
    ctx.lineWidth = lineWidth;

    ctx.beginPath();
    ctx.moveTo(points[0][0], points[0][1]);

    for (let i = 1; i < points.length; i++) {
      ctx.lineTo(points[i][0], points[i][1]);
    }

    ctx.closePath();
    ctx.stroke();

    ctx.fillStyle = color;

    for (const [x, y] of points) {
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  _maxValue(data, idx) {
    let maxPolar = 0;
    for (const d of data) {
      if (d[MAX_DISTANCE_IDX] > maxPolar) { maxPolar = d[idx]; }  // use maxDistance scale
    }

    return maxPolar;
  }

  _pickDistance(max) {
    const MAX_DISTANCES = [100, 250, 1000, 5000, 10000, 20000, 25000, 50000];
    for (const d of MAX_DISTANCES) {
      if (max <= d) {
        return d;
      }
    }
    return MAX_DISTANCES[MAX_DISTANCES.length - 1];
  }

  _drawPolar(id, data) {

    const canvas = this.$refs[id];
    if (!canvas) { return; }

    const ctx = canvas.getContext("2d");

    const w = canvas.width;
    const h = canvas.height;

    ctx.clearRect(0, 0, w, h);

    const cx = w / 2;
    const cy = h / 2;
    const rMax = Math.min(cx, cy) - 4;
    const n = data.length;

    let maxPolar = this._pickDistance(this._maxValue(data, MAX_DISTANCE_IDX));
    const legend = formatUnit(maxPolar, "m");

    this._drawGrid(ctx, cx, cy, rMax, n, legend);
    this._plane(ctx, cx, cy);

    this._drawDatapoints(ctx, data, MAX_DISTANCE_IDX, 2, this._colorSchema.maxDistance, maxPolar);
    this._drawDatapoints(ctx, data, AVG_DISTANCE_IDX, 1, this._colorSchema.avgDistance, maxPolar);
  }

  _renderDefault(html, item) {
    let value = item.value;

    let isNumeric = false;
    let isBitString = false;
    let isStructured = false;

    if (Array.isArray(value)) {
      if (value.length > 0 && value.some((entry) => entry !== null && typeof entry === "object")) {
        isStructured = true;
      } else {
        value = value.join(", ");
      }
    } else if (value && typeof value === "object") {
      isStructured = true;
    } else if (typeof value === "string" && /^[01]+$/.test(value)) {
      isBitString = true;
    } else if (typeof value === "number") {
      isNumeric = true;
    }

    // Render monospace for numeric values or bitStrings
    let style = "";
    if (isBitString) {
      style += "font-family: monospace;font-size: 8px;";
    }
    if (isNumeric) {
      style += "font-family: monospace;";
    }
    if (typeof item.name === "string" && item.name.endsWith(":dts")) {
      style += "font-family: monospace;font-size: 6px;white-space: pre-wrap;word-break: break-all;line-height: 1.05;";
    }

    // Error tags will be rendered orange when the value is != 0
    if (
      typeof item.name === "string" &&
      item.name.endsWith(":err") &&
      typeof item.value === "number" &&
      item.value > 0
    ) {
      style += " color: orange;";
    }

    return { value, style, isStructured };
  }

  _renderStructuredValue(html, value) {
    if (value === null || value === undefined) {
      return html`<span style="color:#999">null</span>`;
    }

    if (Array.isArray(value)) {
      if (value.length === 0) {
        return html`<span style="color:#999">[]</span>`;
      }

      const isFlat = value.every((entry) => entry === null || typeof entry !== "object");
      if (isFlat) {
        return html`${value.join(", ")}`;
      }

      return html`
        <table style="width:100%; border-collapse:collapse; margin:0">
          <tbody>
            ${value.map((entry, index) => html`
              <tr>
                <th scope="row" style="width:24px; text-align:left; vertical-align:top; padding:1px 6px 1px 0; color:#777; font-weight:600">${index}</th>
                <td style="padding:1px 0; vertical-align:top">${this._renderStructuredValue(html, entry)}</td>
              </tr>
            `)}
          </tbody>
        </table>
      `;
    }

    if (typeof value === "object") {
      const entries = Object.entries(value);
      if (entries.length === 0) {
        return html`<span style="color:#999">{}</span>`;
      }

      return html`
        <table style="width:100%; border-collapse:collapse; margin:0">
          <tbody>
            ${entries.map(([key, nestedValue]) => html`
              <tr>
                <th scope="row" style="width:33%; text-align:left; vertical-align:top; padding:1px 6px 1px 0; color:#777; font-weight:600; word-break:break-word">${key}</th>
                <td style="padding:1px 0; vertical-align:top">${this._renderStructuredValue(html, nestedValue)}</td>
              </tr>
            `)}
          </tbody>
        </table>
      `;
    }

    return html`${value}`;
  }

  _row(html, item) {
    // Variables starting with a _ are hidden
    if (item.name.startsWith('_')) return html``;

    // Handle special case for polar diagram
    if (item.name.endsWith(":AntPolar") && Array.isArray(item.value)) {
      const id = `polar-${item.name}`;

      const avgDistance = formatUnit(this._maxValue(item.value, AVG_DISTANCE_IDX), "m");
      const maxDistance = formatUnit(this._maxValue(item.value, MAX_DISTANCE_IDX), "m");

      const name = item.name.split(':')[0];

      return html`
      <tr class="monitor-polar-row">
        <td colspan="2">
          <div class="polar-monitor-card">
            <strong class="polar-monitor-card__name">${name}</strong>
            <div class="polar-monitor">
              <canvas class="polar-monitor__chart" ref="${id}" width="120" height="120"></canvas>
              <div class="polar-monitor__legend">
                <div class="polar-monitor__legend-item">
                  <span class="polar-monitor__swatch" style="background:${this._colorSchema.avgDistance}"></span>
                  <span>
                    <span class="polar-monitor__label">Average distance</span>
                    <strong class="polar-monitor__value">${avgDistance}</strong>
                  </span>
                </div>

                <div class="polar-monitor__legend-item">
                  <span class="polar-monitor__swatch" style="background:${this._colorSchema.maxDistance}"></span>
                  <span>
                    <span class="polar-monitor__label">Maximum distance</span>
                    <strong class="polar-monitor__value">${maxDistance}</strong>
                  </span>
                </div>
              </div>
            </div>
          </div>
        </td>
      </tr>
    `;
    }

    if (item.name.endsWith(":aoa")) {
      const title = item.name.split(':')[0];
      let aircraft = [];

      if (Array.isArray(item.value)) {
        aircraft = item.value.map((entry) => {
          if (typeof entry === "string") {
            const [hex, ds, dis, pressureAlt] = entry.split(",");
            return {
              hex: hex || "UNK",
              ds: ds || "UNKNOWN",
              dis: dis === undefined || dis === "" ? null : Number(dis),
              pressureAlt: pressureAlt === undefined || pressureAlt === "" ? null : Number(pressureAlt),
            };
          }
          return {
            hex: entry.hex ?? entry.address ?? "UNK",
            ds: entry.ds ?? entry.datasource ?? entry.dataSource ?? "UNKNOWN",
            dis: entry.dis ?? entry.distance ?? entry["distance:m"] ?? null,
            pressureAlt: entry.pressureAlt ?? null,
          };
        });
      } else if (item.value && typeof item.value === "object") {
        const hex = item.value.hex ?? [];
        const ds = item.value.ds ?? [];
        const dis = item.value.dis ?? [];
        const pressureAlt = item.value.pressureAlt ?? [];
        const len = Math.max(hex.length, ds.length, dis.length, pressureAlt.length);
        for (let i = 0; i < len; i++) {
          aircraft.push({
            hex: hex[i] ?? "UNK",
            ds: ds[i] ?? "UNKNOWN",
            dis: dis[i] ?? null,
            pressureAlt: pressureAlt[i] ?? null,
          });
        }
      }

      aircraft.sort((a, b) => (a.dis ?? Infinity) - (b.dis ?? Infinity));

      return html`
        <tr>
          <th style="width:33%; vertical-align:top; white-space:nowrap" scope="row">${title}</th>
          <td>
            <div style="display:flex; flex-direction:column; gap:8px">
              <div style="display:grid; grid-template-columns: 60px 55px 60px 75px; gap:8px; font-size:11px; color:#777">
                <span>Hex</span><span>Source</span><span>Distance</span><span>Pressure alt</span>
              </div>
              ${aircraft.map((aircraft) => {
        const distance = aircraft.dis == null ? "—" : `${aircraft.dis} m`;
        const hex = aircraft.hex ?? "UNK";
        const dataSource = aircraft.ds ?? "UNKNOWN";
        const pressureAlt = aircraft.pressureAlt == null ? "—" : `${aircraft.pressureAlt} m`;
        return html`
                  <div style="display:grid; grid-template-columns: 60px 55px 60px 75px; gap:8px; align-items:center; font-size:11px; line-height:1.2; padding:2px 0">
                    <span style="font-family:monospace; font-weight:700;">${hex}</span>
                    <span style="">${dataSource}</span>
                    <span style="text-align:left; font-variant-numeric: tabular-nums;">${distance}</span>
                    <span style="text-align:left; font-variant-numeric: tabular-nums;">${pressureAlt}</span>
                  </div>
                `;
      })}
            </div>
          </td>
        </tr>
      `;
    }

    if (item.name.endsWith(":dts")) {
      const result = formatUnit2(item.name, item.value);
      const lines = String(result.value).split("\n");
      const name = result.name;

      return html`
        <tr>
          <th style="width:33%" scope="row">${name}</th>
          <td style="font-family: monospace;">
            <div style="display:flex; flex-direction:column; gap:4px">
              ${lines.reduce((acc, line, index) => {
        if (index % 2 === 0) {
          acc.push(html`<div style="font-size:10px; font-weight:600; line-height:1.1">${line}</div>`);
        } else {
          acc.push(html`<div style="font-size:6px; line-height:1.05; word-break:break-all">${line}</div>`);
        }
        return acc;
      }, [])}
            </div>
          </td>
        </tr>
      `;
    }

    const dsColorMap = {
      "OGN": "#cb6827",
      "Flarm": "#31a84a",
      "ADSL": "#303a80",
      "ADSL Hdr": "#596eef",
      "Fanet": "#674ea7",
      "ADSB": "#fdce03",
      "PAW": "#ce1f28",
      "NOOP": "#555",
      "ADSL FLARM": "#66bb6a",
      "ADSL OGN": "#ff9800",
      "NONE": "#333",
    };

    if (item.name.endsWith(":rrx") && Array.isArray(item.value)) {
      const name = item.name.split(':')[0];
      // entries are pre-expanded flat RxTiming: {ds, s, e, ch} — no wrapping needed
      const entries = item.value;
      const SEC = 1000;
      const barW = 200; // px per 1000ms
      const compactMode = this._isCompactTimeline();

      const totalMs = Math.ceil(Math.max(...entries.map((e) => e.e), SEC) / SEC) * SEC;
      const totalW = (totalMs / SEC) * barW;


      const dsColor = (ds) => dsColorMap[ds] || "#888";

      const renderSegment = (entry, rowStart, rowEnd, rowWidthMs) => {
        const segStart = Math.max(entry.s, rowStart);
        const segEnd = Math.min(entry.e, rowEnd);
        if (segEnd <= segStart) {
          return null;
        }

        const left = ((segStart - rowStart) / rowWidthMs) * 100;
        const w = Math.max(1, ((segEnd - segStart) / rowWidthMs) * 100);
        const continuesLeft = entry.s < rowStart;
        const continuesRight = entry.e > rowEnd;

        return html`
          <div
            style="position:absolute; left:${left}%; top:1px; width:${w}%; height:14px; background:${dsColor(entry.ds)}; opacity:0.85; border-radius:6px; box-sizing:border-box; ${continuesLeft ? 'border-left:2px solid rgba(255,255,255,0.85);' : ''} ${continuesRight ? 'border-right:2px solid rgba(255,255,255,0.85);' : ''}"
            title="${entry.ds} ch${entry.ch} ${entry.s}-${entry.e}ms"
          >
            <span style="display:flex; align-items:center; justify-content:center; width:100%; height:100%; overflow:hidden; white-space:nowrap; text-overflow:ellipsis; font-size:10px; line-height:14px; color:#fff; text-shadow:0 1px 1px rgba(0,0,0,0.45); pointer-events:none">${entry.ch}</span>
          </div>`;
      };

      const renderCompactTimeline = () => {
        const rows = Array.from({ length: Math.max(1, Math.ceil(totalMs / SEC)) }, (_, i) => ({
          start: i * SEC,
          end: Math.min((i + 1) * SEC, totalMs),
        }));

        return html`
          <div style="display:flex; flex-direction:column; gap:4px">
            ${rows.map((row, rowIndex) => html`
              <div style="display:flex; align-items:center; gap:6px; ${rowIndex > 0 ? 'border-top:1px dashed #ddd; padding-top:4px;' : ''}">
                <span style="width:54px; flex-shrink:0; font-size:9px; color:#777; text-align:right">
                  ${Math.floor(row.start / SEC)}-${Math.ceil(row.end / SEC)}s
                </span>
                <div style="position:relative; flex:1; min-width:0; height:16px; background:#eee; border-radius:2px; overflow:hidden">
                  ${entries.map((entry) => renderSegment(entry, row.start, row.end, SEC))}
                </div>
              </div>
            `)}
          </div>`;
      };

      // Render a single bar for one entry
      const renderBar = (entry) => {
        const left = (entry.s / totalMs) * totalW;
        const w = Math.max(1, ((entry.e - entry.s) / totalMs) * totalW);
        return html`<div style="position:absolute; left:${left}px; text-align:center; width:${w}px; height:100%; background:${dsColor(entry.ds)}; opacity:0.8; border-radius:6px" title="${entry.ds} ch${entry.ch} ${entry.s}-${entry.e}ms">${entry.ch}</div>`;
      };

      const ticks = Array.from({ length: totalMs / 200 + 1 }, (_, i) => i * 200);

      return html`
        <tr>
          <th style="width:33%; vertical-align:top" scope="row">${name}</th>
          <td>
            <div style="font-size:11px; line-height:1.6">
              ${compactMode ? renderCompactTimeline() : html`
                <!-- Schedule: cumulative entries -->
                <div style="position:relative; width:${totalW}px; height:16px; background:#eee; border-radius:2px">
                  ${entries.map((e) => renderBar(e))}
                </div>

                <!-- Tick marks -->
                <div style="position:relative; width:${totalW}px; height:10px; margin-top:2px">
                  ${ticks.map((ms) => {
                    const left = (ms / totalMs) * totalW;
                    const bold = ms % SEC === 0;
                    return html`<span style="position:absolute; left:${left}px; font-size:8px; color:${bold ? '#555' : '#aaa'}; transform:translateX(-50%); font-weight:${bold ? 'bold' : 'normal'}">${ms}</span>`;
                  })}
                </div>
              `}

              <!-- Legend -->
              <br />
              <div style="display:flex; flex-wrap:wrap; gap:6px; margin-top:4px">
                ${[...new Set(entries.map((e) => e.ds))].map((ds) => html`
                  <span style="display:flex; align-items:center; gap:3px">
                    <span style="display:inline-block; width:10px; height:10px; background:${dsColor(ds)}; border-radius:2px; opacity:0.8"></span>
                    <span style="color:#555">${ds}</span>
                  </span>
                `)}
              </div>

            </div>
          </td>
        </tr>
      `;
    }

    if (item.name.endsWith(":rtx") && Array.isArray(item.value)) {
      const name = item.name.split(':')[0];
      const protocols = item.value; // [{ds, slots:[{s,e,ch},...]}]
      const SEC = 1000;
      const barW = 200; // px per 1000ms
      const compactMode = this._isCompactTimeline();

      const allSlots = protocols.flatMap((p) => p.slots || []);
      const totalMs = Math.ceil(Math.max(...allSlots.map((s) => s.e), SEC) / SEC) * SEC;
      const totalW = (totalMs / SEC) * barW;

      const dsColor = (ds) => dsColorMap[ds] || "#888";

      const renderSegment = (proto, slot, rowStart, rowEnd, rowWidthMs) => {
        const color = dsColor(proto.ds);
        const segStart = Math.max(slot.s, rowStart);
        const segEnd = Math.min(slot.e, rowEnd);
        if (segEnd <= segStart) {
          return null;
        }

        const left = ((segStart - rowStart) / rowWidthMs) * 100;
        const w = Math.max(1, ((segEnd - segStart) / rowWidthMs) * 100);
        const continuesLeft = slot.s < rowStart;
        const continuesRight = slot.e > rowEnd;

        return html`
          <div
            style="position:absolute; left:${left}%; top:1px; width:${w}%; height:14px; background:${color}; opacity:0.85; border-radius:6px; box-sizing:border-box; ${continuesLeft ? 'border-left:2px solid rgba(255,255,255,0.85);' : ''} ${continuesRight ? 'border-right:2px solid rgba(255,255,255,0.85);' : ''}"
            title="${proto.ds} ch${slot.ch} ${slot.s}-${slot.e}ms"
          >
            <span style="display:flex; align-items:center; justify-content:center; width:100%; height:100%; overflow:hidden; white-space:nowrap; text-overflow:ellipsis; font-size:10px; line-height:14px; color:#fff; text-shadow:0 1px 1px rgba(0,0,0,0.45); pointer-events:none">${slot.ch}</span>
          </div>`;
      };

      const renderProtocolRow = (proto) => {
        const color = dsColor(proto.ds);
        const hasTiming = proto.min != null;
        const avg = hasTiming ? ((proto.min + proto.max) / 2 / 1000).toFixed(1) : null;
        const rows = compactMode ? Array.from({ length: Math.max(1, Math.ceil(totalMs / SEC)) }, (_, i) => ({
          start: i * SEC,
          end: Math.min((i + 1) * SEC, totalMs),
        })) : [];
        return html`
          <div style="display:flex; flex-direction:column; gap:4px; margin-bottom:4px">
            <div style="display:flex; align-items:center; gap:6px">
            <span style="width:70px; font-size:10px; color:#555; text-align:right; flex-shrink:0">${proto.ds}</span>
              ${compactMode
            ? html`
                <div style="display:flex; flex:1; flex-direction:column; gap:4px; min-width:0">
                  ${rows.map((row, rowIndex) => html`
                    <div style="display:flex; align-items:center; gap:6px; ${rowIndex > 0 ? 'border-top:1px dashed #ddd; padding-top:4px;' : ''}">
                      <span style="width:42px; flex-shrink:0; font-size:9px; color:#777; text-align:right">${Math.floor(row.start / SEC)}-${Math.ceil(row.end / SEC)}s</span>
                      <div style="position:relative; flex:1; min-width:0; height:16px; background:#eee; border-radius:2px; overflow:hidden">
                        ${(proto.slots || []).map((ts) => renderSegment(proto, ts, row.start, row.end, SEC))}
                      </div>
                    </div>
                  `)}
                </div>
              `
            : html`
                <div style="position:relative; width:${totalW}px; height:16px; background:#eee; border-radius:2px">
                  ${(proto.slots || []).map((ts) => {
                    const left = (ts.s / totalMs) * totalW;
                    const w = Math.max(1, ((ts.e - ts.s) / totalMs) * totalW);
                    return html`<div style="position:absolute; left:${left}px; width:${w}px; height:100%; background:${color}; opacity:0.8; border-radius:6px; display:flex; align-items:center; justify-content:center; overflow:hidden" title="${proto.ds} ch${ts.ch} ${ts.s}-${ts.e}ms"><span style="font-size:10px; line-height:14px; color:#fff; text-shadow:0 1px 1px rgba(0,0,0,0.45); pointer-events:none">${ts.ch}</span></div>`;
                  })}
                </div>
              `}
            </div>
            ${hasTiming ? html`<span style="width:130px; font-size:10px; color:#999; flex-shrink:0">${proto.min}-${proto.max}ms ~${avg}s</span>` : ''}
          </div>
        `;
      };

      const ticks = Array.from({ length: totalMs / 200 + 1 }, (_, i) => i * 200);

      return html`
        <tr>
          <th style="width:33%; vertical-align:top" scope="row">${name}</th>
          <td>
            <div style="font-size:11px; line-height:1.6">
              ${protocols.map((p) => renderProtocolRow(p))}

              ${compactMode ? html`` : html`
                <!-- Tick marks -->
                <div style="display:flex; align-items:center; gap:6px">
                  <span style="width:70px; flex-shrink:0"></span>
                  <div style="position:relative; width:${totalW}px; height:10px; margin-top:2px">
                    ${ticks.map((ms) => {
                      const left = (ms / totalMs) * totalW;
                      const bold = ms % SEC === 0;
                      return html`<span style="position:absolute; left:${left}px; font-size:8px; color:${bold ? '#555' : '#aaa'}; transform:translateX(-50%); font-weight:${bold ? 'bold' : 'normal'}">${ms}</span>`;
                    })}
                  </div>
                </div>
              `}
            </div>
          </td>
        </tr>
      `;
    }

    const rendered = this._renderDefault(html, item);
    const result = rendered.isStructured ? { name: item.name, value: this._renderStructuredValue(html, rendered.value) } : formatUnit2(item.name, rendered.value);
    return html`
    <tr>
      <th style="width:33%" scope="row">${result.name}</th>
      <td class="monitor-value ${rendered.isStructured ? "is-structured" : ""}" style="${rendered.style}">${result.value}</td>
    </tr>
  `;
  }

  _filteredItems() {
    return this.state.data.filter((i) => true);
  }

  render(html) {
    let items = this._filteredItems();
    return html`
      <section class="page-section page-section--flat monitor-section">
        <header class="monitor-heading">
          <p class="monitor-heading__eyebrow">Live monitoring</p>
          <h2>${this.selected}</h2>
          <p>Module diagnostics and counters update automatically.</p>
        </header>
        <div class="table-wrap">
        <table class="data-table monitor-table">
          <tbody>
            ${items.map((item) => html` ${this._row(html, item)} `)}
          </tbody>
        </table>
        </div>
      </section>
    `;
  }
}

customElements.define("monitor-module", MonitorModule);

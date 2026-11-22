const UNIT_TABLE = {
    hz: [
        { scale: 1e9, suffix: "GHz", min: 1e9, digits: 3 },
        { scale: 1e6, suffix: "MHz", min: 1e6, digits: 3 },
        { scale: 1e3, suffix: "kHz", min: 1e3, digits: 1 },
        { scale: 1, suffix: "Hz", min: 0, digits: 0 },
    ],

    kb: [
        { scale: 1e6, suffix: "MB", min: 1e6, digits: 1 },
        { scale: 1e3, suffix: "KB", min: 1e3, digits: 1 },
        { scale: 1, suffix: "B", min: 0, digits: 1 },
    ],
    k: [
        { scale: 1e3, suffix: "K", min: 1e3, digits: 3 },
        { scale: 1, suffix: "", min: 0, digits: 0 },
    ],
    deg: [
        { scale: 1, suffix: "Deg", min: 0, digits: 1 },
    ],
    m: [
        { scale: 1e3, suffix: "Km", min: 1e3, digits: 1 },
        { scale: 1, suffix: "m", min: 0, digits: 0 },
    ],
    msec: [
        { scale: 1, suffix: "ms", min: 0, digits: 0 },
    ],
    usec: [
        { scale: 1e3, suffix: "ms", min: 1e3, digits: 1 },
        { scale: 1, suffix: "us", min: 0, digits: 0 },
    ],
    ms: [
        { scale: 0.277777778, suffix: "Km/h", min: 2.77777778, digits: 1 },
        { scale: 1, suffix: "m/s", min: 0, digits: 1 },
    ],
    dbm: [
        { scale: 1, suffix: "dBm", min: 0, digits: 0 },
    ],
    psec: [
        { scale: 1, suffix: "per/sec", min: 0, digits: 0 },
    ],
    err: [
        { scale: 1, suffix: " (errors)", min: 0, digits: 0 },
    ],
    // m/s to kt
    kt: [
        { scale: 0.514444444, suffix: "Kt", min: 0, digits: 1 },
    ],
    // meter to miles
    mi: [
        { scale: 1852.02333549, suffix: "Nm", min: 500, digits: 2 },
        { scale: 0.3047999995, suffix: "ft", min: 0, digits: 0 },
    ],
    // meter to ft
    ft: [
        { scale: 0.3047999995, suffix: "ft", min: 0, digits: 0 },
    ],

};

const formatTime = (seconds, locale = navigator.language) => {
    if (typeof seconds !== 'number' || isNaN(seconds) || seconds < 0) {
        return String(seconds);
    }

    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;

    const parts = [];

    if (hours > 0) {
        parts.push(`${hours}hour${hours !== 1 ? 's' : ''}`);
    }

    if (minutes > 0) {
        parts.push(`${minutes}min${minutes !== 1 ? '' : ''}`);
    }

    if (secs > 0 || parts.length === 0) {
        parts.push(`${secs}sec${secs !== 1 ? 's' : ''}`);
    }

    return parts.join(' ');
};

const findUnit = (name) => {
    const splited = name.split(":");
    if (splited.length == 2) {
        return splited[1];
    }
    return "";
};

/**
 * 
 * @param Take a string of 0 and one, and replace them with big and small dots 
 * @returns 
 */
const bitsToDots = (str) => {
    return str.replace(/1/g, "●").replace(/0/g, "·");
}

const _chunkString = (str, size, sep = "'") => {
    return str.match(new RegExp(`.{1,${size}}`, "g")).join(sep);
}


/**
 * Format a raw metric value according to a unit code used by monitor fields.
 *
 * Supported special unit codes:
 * - `b`: boolean-style output (`✓` when value > 0, otherwise disabled symbol)
 * - `bin`: 8-bit binary view rendered as dots
 * - `el`: elapsed seconds as human-readable time
 * - `dts`: protocol timing bitmap split into 0..500ms and 500..1000ms bands
 *
 * For regular units, formatting rules are resolved from `UNIT_TABLE` and use
 * locale-aware number formatting with fixed fraction digits per rule.
 *
 * @param {number|string} value Raw value to format.
 * @param {string} unit Unit selector (for example `hz`, `m`, `kt`, `b`, `el`).
 * @param {string} [locale=navigator.language] Locale passed to Intl.NumberFormat.
 * @returns {string|number} Formatted display value, or the original value when no unit rule exists.
 */
const formatUnit = (value, unit, locale = navigator.language) => {

    // Binary true or false
    if (unit === 'b') {
        return value > 0 ? '&#10003;' : '&#x292B;';
    }

    // Binary (numbers)
    if (unit === 'bin') {
        const bits = (value >>> 0).toString(2).padStart(8, '0');
        return bitsToDots(bits);
    }

    // time el=elapsed?
    if (unit === 'el') {
        return formatTime(value);
    }

    // Protocol timing stats: show two 500ms halves on separate lines.
    if (unit == 'dts') {
      const chunks = value.match(/.{1,50}/g) || [value];
      value = chunks
            .map((chunk, idx) => {
                const label = idx === 0 ? '0..500ms' : '500..1000ms';
                return `${label}\n${bitsToDots(_chunkString(chunk, 10, "|"))}`;
            })
            .join("\n");
    }

    const table = UNIT_TABLE[unit];
    if (!table) {
        return value;
    }

    const magnitude = Math.abs(value);

    for (const entry of table) {
        if (magnitude >= entry.min) {
            const scaled = value / entry.scale;

            const formatter = new Intl.NumberFormat(locale, {
                minimumFractionDigits: entry.digits,
                maximumFractionDigits: entry.digits,
            });

            return formatter.format(scaled) + entry.suffix;
        }
    }
    return value + unit;
};

const formatUnit2 = (name, value, locale = navigator.language) => {
    const unit = findUnit(name);
    value = formatUnit(value, unit, locale);
    name = name.replace(`:${unit}`, '');

    // Special case for dts, where the variable name is also a unit
    if (unit == 'dts') {
        name = formatUnit(name, 'hz', locale);
    }

    return { name, value }
}

export { formatUnit, formatUnit2, findUnit, bitsToDots };

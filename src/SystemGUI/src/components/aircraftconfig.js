import { El } from "@frameable/el";
import store from "./store";
import JustValidate from "just-validate";

class AircraftConfig extends El {
  created() {
    this.types = [
      "Light",
      "Small",
      "Large",
      "Aerobatic",
      "Helicopter",
      "Glider",
      "Balloon",
      "Sky Diver",
      "Ultra Light",
      "Unmanned aerial vehicle",
      "Surface Emergency Vehicle",
      "Surface Vehicle",
      "Point Obstacle",
      "Gyrocopter",
      "Hang Glider",
      "Para Glider",
      "Drop Plane"];

    this.transponderTypes = ["ICAO", "FLARM", "OGN", "ADSL"];
    this.protocolTypes = ["OGN", "FLARM", "ADSL", "FANET", "ADSB"];
    this.protocolModes = [
      { value: "OFF", label: "OFF" },
      { value: "RX_TX", label: "RX/TX" },
      { value: "RX", label: "RX" },
      { value: "TX", label: "TX" },
    ];

    this.state = this.$observable({ showHelp: false, aircraft: {}, groundStation: false });
    this.copyOfAircraft = {};
    this.protocolModeState = {};
    this._resetProtocolModeState();
    this._fetchData().then((data) => {
      Object.assign(this.state.aircraft, data);
      Object.assign(this.copyOfAircraft, data);
      this._setFormData(this.state.aircraft);
    });
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.callSign, [
        {
          rule: "required",
        },
        {
          rule: "customRegexp",
          value: /^[A-Za-z0-9\-]*$/,
        },
        {
          rule: "minLength",
          value: 3,
        },
        {
          rule: "maxLength",
          value: 7,
        },
      ])
      .addField(this.$refs.address, [
        {
          rule: "required",
        },
        {
          rule: "customRegexp",
          value: /^[a-zA-Z0-9\-]*$/,
        },
        {
          rule: "minLength",
          value: 6,
        },
        {
          rule: "maxLength",
          value: 6,
        },
      ])
      .addField(this.$refs.heightAboveGps, [
        {
          rule: "minNumber",
          value: 0,
        },
        {
          rule: "maxNumber",
          value: 1500,
        },
      ])
      .onSuccess((event) => {
        const aircraft = this._getFormData();
        this._updateData(aircraft).then(() => {
          store.setDefaultAirCraftId(aircraft.callSign).then((data) => {
            this.close();
          });
        });
      });
  }

  _addressFormat(number) {
    number = number & 0xffffff;
    return number !== undefined ? number.toString(16).toUpperCase().padStart(6, "0") : "000000";
  }

  _onGroundStationChange(e) {
    this._updateGroundStationState(e.target.checked);
  }

  _resetProtocolModeState() {
    this.protocolModeState = Object.fromEntries(this.protocolTypes.map((protocol) => [protocol, "OFF"]));
  }

  _protocolRefName(protocol, mode) {
    return `protocol${protocol.toUpperCase()}${mode}`;
  }

  _protocolInputId(protocol, mode) {
    return `protocol_${protocol.toUpperCase()}_${mode}`;
  }

  _protocolLabelRefName(protocol, mode) {
    return `protocolLabel${protocol.toUpperCase()}${mode}`;
  }

  _protocolModeLabelClass(active) {
    return `protocol-choice ${active ? "is-active" : ""}`;
  }

  _setProtocolMode(protocol, mode) {
    this.protocolModeState[protocol] = mode;
    this._updateProtocolModeStyles(protocol);
  }

  _onProtocolModeChange(protocol, mode) {
    this._setProtocolMode(protocol, mode);
  }

  _updateProtocolModeStyles(protocol) {
    for (const mode of this.protocolModes) {
      const labelRef = this.$refs[this._protocolLabelRefName(protocol, mode.value)];
      if (labelRef) {
        const active = this.protocolModeState[protocol] === mode.value;
        labelRef.classList.toggle("is-active", active);
      }
    }
  }

  _normalizeProtocolModes(protocols) {
    const normalized = Object.fromEntries(this.protocolTypes.map((protocol) => [protocol, "OFF"]));
    if (!Array.isArray(protocols)) {
      return normalized;
    }

    for (const protocol of protocols) {
      if (typeof protocol === "string") {
        normalized[protocol] = protocol === "ADSB" ? "RX" : "RX_TX";
        continue;
      }

      if (protocol && typeof protocol === "object") {
        for (const [protocolName, protocolConfig] of Object.entries(protocol)) {
          if (!this.protocolTypes.includes(protocolName)) {
            continue;
          }

          const mode = protocolConfig?.mode ?? "RX_TX";
          normalized[protocolName] = protocolName === "ADSB"
            ? (mode === "TX" || mode === "OFF" ? "OFF" : "RX")
            : (this.protocolModes.some((item) => item.value === mode) ? mode : "RX_TX");
        }
      }
    }

    return normalized;
  }

  _updateGroundStationState(enabled) {
    this.state.groundStation = enabled;
    if (enabled) {
      this.$refs.category.value = "Point Obstacle";
    }
  }

  _resetForm() {
    Object.assign(this.state.aircraft, this.copyOfAircraft);
    this._setFormData(this.state.aircraft);
  }

  _setFormData(aircraft) {
    this.$refs.callSign.value = aircraft.callSign?.toUpperCase() ?? "";
    this.$refs[`category${aircraft.category?.toUpperCase()}`].selected = true;
    this.$refs[`transponderType${aircraft.addressType?.toUpperCase()}`].selected = true;
    this.$refs.address.value = this._addressFormat(aircraft.address).toUpperCase();
    this.$refs.noTrack.checked = aircraft.noTrack;
    this.$refs.privacy.checked = aircraft.privacy ?? false;
    this.$refs.groundStation.checked = aircraft.groundStation;
    this._updateGroundStationState(!!aircraft.groundStation);
    this.$refs.heightAboveGps.value = aircraft.heightAboveGps ?? 0;
    this.protocolModeState = this._normalizeProtocolModes(aircraft.protocols);
    this.protocolTypes.forEach((protocol) => this._updateProtocolModeStyles(protocol));
    //    this.$refs.autoConf.checked = aircraft.autoConf;
  }

  _getFormData() {
    let aircraft = {};
    aircraft.callSign = this.$refs.callSign.value.toUpperCase();
    aircraft.category = this.$refs.category.value;
    aircraft.addressType = this.$refs.transponder.value;
    aircraft.address = Number("0x" + this.$refs.address.value);
    aircraft.noTrack = this.$refs.noTrack.checked;
    aircraft.privacy = this.$refs.privacy.checked;
    aircraft.groundStation = this.$refs.groundStation.checked;
    aircraft.heightAboveGps = aircraft.groundStation ? (Number(this.$refs.heightAboveGps.value) || 0) : 0;
    //    aircraft.autoConf = this.$refs.autoConf.checked;

    aircraft.protocols = [];
    for (let protocol of this.protocolTypes) {
      const mode = this.protocolModeState[protocol] ?? "OFF";
      if (mode !== "OFF") {
        aircraft.protocols.push({
          [protocol]: {
            mode,
          },
        });
      }
    }
    return aircraft;
  }

  _fetchData() {
    if (this.selected) {
      return store.fetch(`/api/Config/aircraft/${this.selected}.json`).catch((e) => {
        Object.assign(this.state.aircraft, {});
      });
    } else {
      return Promise.resolve({
        category: "Light",
        addressType: "ICAO",
        protocols: [
          { OGN: { mode: "RX_TX" } },
          { ADSL: { mode: "RX_TX" } },
        ],
      });
    }
  }

  _updateData(aircraft) {
    return store.updateAircraft(aircraft).catch((e) => {
      Object.assign(this.state.aircraft, {});
    });
  }

  render(html) {
    let help = this.state.showHelp ? this._help(html) : "";
    return html`
    <form ref="form" autocomplete="off" novalidate="novalidate">
      <section class="page-section page-section--flat app-grid app-grid--2">
        <label for="callsign">
          Call Sign:
          <input type="text" id="callsign" ref="callSign" placeholder="Call Sign" ${this.selected ? "disabled" : ""}/>
        </label>
        <label>
          Aircraft Type:
          <select ref="category" required ${this.state.groundStation ? "disabled" : ""}>
          ${this.types.map((item) => html` <option ref="category${item.toUpperCase()}">${item}</option>`)}
          </select>
        </label>
        <label>
          Your official Transponder:
          <select ref="transponder" required>
          ${this.transponderTypes.map((item) => html` <option ref="transponderType${item.toUpperCase()}">${item}</option> `)}
          </select>
        </label>
        <label>
          Transponder Code:
          <input type="text" id="address" ref="address" placeholder="000000" />
        </label>
      </section>

      <section class="page-section page-section--flat aircraft-protocols app-grid app-grid--2">
        <div>
            <h3>Protocol modes</h3>

            ${this.protocolTypes.map(
              (item) => html`
                <div class="protocol-row">
                  <div class="fw-bold">${item === "ADSB" ? "ADS-B (1090 MHz)" : item}</div>
                  <div class="protocol-options">
                    ${this.protocolModes.filter((mode) => item !== "ADSB" || mode.value === "OFF" || mode.value === "RX").map(
                      (mode) => html`
                        <label class="${this._protocolModeLabelClass(this.protocolModeState[item] === mode.value)}" ref="${this._protocolLabelRefName(item, mode.value)}" for="${this._protocolInputId(item, mode.value)}">
                          <input
                            type="radio"
                            id="${this._protocolInputId(item, mode.value)}"
                            name="protocol${item.toUpperCase()}"
                            ref="${this._protocolRefName(item, mode.value)}"
                            value="${mode.value}"
                            ${this.protocolModeState[item] === mode.value ? "checked" : ""}
                            onchange=${() => this._onProtocolModeChange(item, mode.value)}
                          />
                          <span>${mode.label}</span>
                        </label>
                      `,
                    )}
                  </div>
                </div>
              `,
            )}

        </div>
        <div>

          <label for="privacy">
            <input type="checkbox" id="privacy" ref="privacy" />Privacy
          </label>
          <small>Keeps this aircraft out of the GATAS Connect fleet.</small>

          <!--
          <label for="autoConf">
            <input type="checkbox" id="autoConf" ref="autoConf" />Auto Configure
          </label>
          <small>When enabled, GATAS will automatically treat this aircraft as yours if it detects a mismatched transponder code via ADS-B or MLAT, based on matching location and movement. It will then reconfigure GATAS accordingly. </small>
          -->

          <label for="noTrack">
            <input type="checkbox" id="noTrack" ref="noTrack" />No Track
          </label>
          <small>GaTas will indicate to other receivers that you don't want to be tracked.</small>
        </div>
      </section>
      <hr />
      <div>
        <label for="groundStation">
        <input type="checkbox" id="groundStation" ref="groundStation" onchange=${this._onGroundStationChange} />Ground Station
        </label>
        <small>When enabled, this devices acts as a ground station.<br /> Most importantly, it will send additional traffic using ADSL Uplink to other aircraft.</small>
        <div style="display: ${this.state.groundStation ? 'block' : 'none'}">
          <label for="heightAboveGps">
            Height above GaTas (m):
            <input type="number" id="heightAboveGps" ref="heightAboveGps" placeholder="0" />
          </label>
          <small>Height of the static object above the GaTas device in meters.</small>
        </div>
      </div>

      <!-- Buttons -->
      <div class="form-actions form-actions--4">
        <input class="secondary" type="button" value="Help" onclick=${() => (this.state.showHelp = true)} />
        <input class="secondary" type="button" value="Cancel" onclick=${this.close} />
        <input class="secondary" type="button" value="Reset" onclick=${this._resetForm} />
        <input type="submit" value="Save" />
      </div>
    </form>

    ${help}
    `;
  }

  _help(html) {
    return html`
    <div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="aircraft-help-title">
      <div class="app-modal__surface">
        <article>
          <header>
              <h4 id="aircraft-help-title">Aircraft configuration help</h4>
          </header>
          <div class="app-modal__body">
              <p>
                <b>Your official Transponder</b><br />
                <ul>
                  <li>If you have a mode-s transponder, use that code, select ADSL under <i>Your official Transponder</i>.
                  <li>If you only have a FLARM transponder, use the code from that device, select FLARM under <i>Your official Transponder</i>.
                  <li>For OGN/ADSL transponders use the code from these devices, select OGN/ADSL under <i>Your official Transponder</i>.
                </ul>
                From the above protocols, only enable the protocols of the devices you don't own yet to
                avoid duplicate transmissions on the same protocol.
            </p>

            <p>
            <b>Protocol Modes</b><br />
            Select whether each protocol should be disabled, receive only, transmit only, or both receive and transmit. To enable ADS-B use the correct module under 'Modules'.
            </p>

            <p>
              <b>Ground station:</b><br />
              <ul>
                <li> it transmits the closest 10 aircraft pver ADS-L O-band
                <li> Allows you to set a altitde of the static object, this indicating surrounding traffic something is here. Ideal for (hang) glider fields
                <li> Transponder will be forced to be ADS-L
              </ul>
            </p>
          </div>
          <footer class="app-modal__actions app-modal__actions--single">
              <button type="button" onclick=${() => (this.state.showHelp = false)}>Close</button>
          </footer>
        </article>
      </div>
    </div>`;
  }
}

customElements.define("aircraft-config", AircraftConfig);

import { El } from "@frameable/el";
import store from "./store";
import JustValidate from "just-validate";
import { icon, portValidation, ipValidation, ssidValidation, passwordValidation, configStringLengthValidator } from "./utils";

class ModuleConfig extends El {
  _initForm(dataFuture) {
    this.state = this.$observable({ data: {} });
    this.copyOfData = {};
    return dataFuture
      .then((data) => {
        Object.assign(this.state.data, data);
        Object.assign(this.copyOfData, data);
        this._setFormData(this.state.data);
        return data;
      })
      .catch((e) => {
        Object.assign(this.state.data, {});
      });
  }

  _resetForm() {
    Object.assign(this.state.data, this.copyOfData);
    this._setFormData(this.state.data);
  }

  buttonArray(html) {
    return html` <!-- Buttons -->
      <div class="form-actions form-actions--3">
        <input class="secondary" type="button" value="Cancel" onclick=${this.close} />
        <input class="secondary" type="button" value="Reset" onclick=${this._resetForm} />
        <input class="primary-action" type="submit" value="Save" />
      </div>`;
  }
}

class AircraftTrackerConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("AircraftTracker"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator.onSuccess((event) => {
      const data = this._getFormData();
      store.updateModuleData("AircraftTracker", { ...this.copyOfData, ...data }).then(() => {
        this.close();
      });
    });
  }

  _setFormData(data) {
    this.$refs.ddbEnabled.checked = data.ddbEnabled;
    this.$refs.pathPredictionEnabled.checked = data.ppEnabled;
    this.$refs.prefixEnabled.checked = data.prefixEnabled;
    this.$refs.showSquawk.checked = data.showSquawk;
  }

  _getFormData() {
    return {
      ddbEnabled: this.$refs.ddbEnabled.checked,
      ppEnabled: this.$refs.pathPredictionEnabled.checked,
      prefixEnabled: this.$refs.prefixEnabled.checked,
      showSquawk: this.$refs.showSquawk.checked,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the Aircraft Tracker</h4>
      <p>
        Aircraft tracker receives all aircraft from ADSB, ADSL and other enabled protocols and keeps track of them. It will send on a 1 second heartbeat new
        calculated positions (when enabled) and will ensure that attached EFBs get regular updates.
      </p>
      <form ref="form" autocomplete="off" novalidate="novalidate">

      <div class="page-section">
          <div class="config-field-heading">
            <label for="ddbEnabled">DDB Enabled</label>
            <span class="help-label" tabindex="0" aria-label="About DDB Enabled">
              ${html.raw(icon.help)}
              <span class="app-tooltip" role="tooltip">
                Look up aircraft callsigns by hex code using the
                <a href="https://ddb.glidernet.org" target="_blank" rel="noopener noreferrer">DDB</a>
                for connected GDL90 or NMEA clients.
              </span>
            </span>
          </div>
          <input type="checkbox" id="ddbEnabled" ref="ddbEnabled" />
          <div class="notice notice--warning">
            <svg class="icon-spacing" style="width: 24px; height: 24px;" viewBox="0 0 24 24"><path fill="currentColor" d="M13 14h-2V9h2m0 9h-2v-2h2M1 21h22L12 2 1 21z"></path></svg>
            Please note that the DDB is created and maintained by aviation enthusiasts and is in no way an official registry.
            Because of this, you may encounter aircraft whose callsign differs from what ATC uses.
          </div>
        </div>

        <div class="page-section">
          <div class="config-field-heading">
            <label for="pathPredictionEnabled">Path prediction</label>
            <span class="help-label" tabindex="0" aria-label="About Path prediction">
              ${html.raw(icon.help)}
              <span class="app-tooltip" role="tooltip">
                Extrapolate short gaps between received aircraft positions before forwarding tracker updates to connected clients.
                Path prediction estimates missing positions and can correct positions received late, such as through mobile ADS-B connections. It predicts the aircraft's current location using airspeed, vertical velocity, and turn rate.
              </span>
            </span>
            <span class="experimental-badge" aria-label="Experimental feature">
              <svg aria-hidden="true" viewBox="0 0 24 24"><path d="M12 3 2.5 20h19L12 3Zm0 5.2 4.9 8.8H7.1L12 8.2Zm-1 2.8v3.2h2V11h-2Zm0 4.2v2h2v-2h-2Z" fill="currentColor"></path></svg>
              Experimental
            </span>
          </div>
          <input type="checkbox" id="pathPredictionEnabled" ref="pathPredictionEnabled" />

        </div>

        <div class="page-section">
          <div class="config-field-heading">
            <label for="prefixEnabled">Add Data Source Prefix</label>
            <span class="help-label" tabindex="0" aria-label="About Add Data Source Prefix">
              ${html.raw(icon.help)}
              <span class="app-tooltip" role="tooltip">
                Prefix tracked callsigns with the two-letter datasource code, for example <code>OG</code> or <code>FL</code>, so connected EFBs can show how the traffic was received.
              </span>
            </span>
          </div>
          <input type="checkbox" id="prefixEnabled" ref="prefixEnabled" />
        </div>

        <div class="page-section">
          <div class="config-field-heading">
            <label for="showSquawk">Show Squawk</label>
            <span class="help-label" tabindex="0" aria-label="About Show Squawk">
              ${html.raw(icon.help)}
              <span class="app-tooltip" role="tooltip">
                Show a known squawk code instead of the aircraft callsign.
              </span>
            </span>
          </div>
          <input type="checkbox" id="showSquawk" ref="showSquawk" />
        </div>
        <br />
        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("aircrafttracker-config", AircraftTrackerConfig);

class DataPortConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("DataPort"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator.onSuccess(() => {
      const data = this._getFormData();
      store.updateModuleData("DataPort", { ...this.copyOfData, ...data }).then(() => {
        this.close();
      });
    });
  }

  _setFormData(data) {
    this.$refs.pflauEnabled.checked = data.pflauEnabled === true || data.pflauEnabled === 1;
  }

  _getFormData() {
    return {
      pflauEnabled: this.$refs.pflauEnabled.checked,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the DataPort Module</h4>
      <p>DataPort generates NMEA-compatible sentences for connected electronic flight bags.</p>
      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section">
          <div class="config-field-heading">
            <label for="pflauEnabled">Send PFLAU heartbeat</label>
            <span class="help-label" tabindex="0" aria-label="About sending PFLAU">
              ${html.raw(icon.help)}
              <span class="app-tooltip" role="tooltip">
                Send FLARM heartbeat, receiver count, transmission status, and GPS status once per second.
              </span>
            </span>
          </div>
          <input type="checkbox" id="pflauEnabled" ref="pflauEnabled" />
          <div class="notice notice--warning">
            <div>
              Users of the RB Avionics RB05 module must enable PFLAU. It might show INOP in the display otherwise.<br />
              SkyDemon users may need to disable PFLAU; see the
              <a href="http://forums.skydemon.aero/Topic32128.aspx" target="_blank" rel="noopener noreferrer">
                SkyDemon forum discussion
              </a>
              for more information.
            </div>
          </div>
        </div>
        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("dataport-config", DataPortConfig);

class Gdl90ServiceConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("Gdl90Service"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);
    validator.onSuccess(() => {
      const data = this._getFormData();
      store.updateModuleData("Gdl90Service", { ...this.copyOfData, ...data }).then(() => {
        this.close();
      });
    });
  }

  _setFormData(data) {
    this.$refs.mslAltFallback.checked = data.mslAltFallback === true || data.mslAltFallback === 1;
  }

  _getFormData() {
    return {
      mslAltFallback: this.$refs.mslAltFallback.checked,
    };
  }

  render(html) {
    const mslAltFallback = this.state?.data?.mslAltFallback === true || this.state?.data?.mslAltFallback === 1;
    return html`
      <h4>Configuration of the GDL90 Service</h4>
      <p>
        GDL90 requires pressure altitude for its ownship report. You can add a BMP280 pressure sensor;
        see the <a href="https://gatasconspicuity.github.io/gatasDoc/bmp280/" target="_blank" rel="noopener noreferrer">BMP280 setup guide</a>.
      </p>
      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section">
          <div class="config-field-heading">
            <label for="mslAltFallback">Use MSL altitude when pressure altitude is unavailable</label>
            <span class="help-label" tabindex="0" aria-label="About MSL altitude fallback">
              ${html.raw(icon.help)}
              <span class="app-tooltip" role="tooltip">
                When disabled, GDL90 sends pressure altitude as unavailable if no pressure altitude is available. When enabled, GDL90 substitutes GPS-derived MSL altitude.
              </span>
            </span>
          </div>
          <input
            type="checkbox"
            id="mslAltFallback"
            ref="mslAltFallback"
            onchange=${(event) => { this.state.data.mslAltFallback = event.target.checked; }}
          />
          ${mslAltFallback
            ? html`<div class="notice notice--warning">
                MSL altitude is not pressure altitude. Use this fallback for conspicuity only; do not rely on it for flying or vertical separation.
              </div>`
            : ""}
        </div>
        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("gdl90service-config", Gdl90ServiceConfig);

class WifiServiceConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("WifiService"));
    this.clientsIds = [0, 1, 2, 3];
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.apssid, [
        {
          rule: "required",
        },
        ...ssidValidation,
      ])
      .addField(this.$refs.appassword, [
        {
          rule: "required",
        },
        ...passwordValidation,
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData("WifiService", { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });

    this.clientsIds.forEach((idx) => {
      validator.addField(this.$refs[`apssid_${idx}`], [...ssidValidation]).addField(this.$refs[`appassword_${idx}`], [...passwordValidation]);
    });
  }

  _setFormData(data) {
    // Get Access Point
    if (data == null || data.ap == null || data.ap.password == null || data.ap.password.length < 8 || data.ap.ssid == null || data.ap.ssid.length < 3) {
      this.$refs.apssid.value = "GATAS";
      this.$refs.appassword.value = "12345678";
    } else {
      this.$refs.apssid.value = data.ap.ssid;
      this.$refs.appassword.value = data.ap.password;
    }

    // this.$refs.apDisabled.checked = data.ap.disabled;

    // Get Clients
    if (data != null && data.clients != null) {
      data.clients.forEach((client, idx) => {
        this.$refs[`apssid_${idx}`].value = client.ssid;
        this.$refs[`appassword_${idx}`].value = client.password;
      });
    }
  }

  _getFormData() {
    let clients = [];
    this.clientsIds.forEach((idx) => {
      if (this.$refs[`apssid_${idx}`].value && this.$refs[`appassword_${idx}`].value) {
        clients.push({ ssid: this.$refs[`apssid_${idx}`].value, password: this.$refs[`appassword_${idx}`].value });
      }
    });

    return {
      ap: {
        ssid: this.$refs.apssid.value,
        password: this.$refs.appassword.value,
        // disabled: this.$refs.apDisabled.checked,
      },
      clients: clients,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the Wifi Service</h4>
      <p>
        This module connects to a Wifi Network in the following order:<br>
        <ol>
          <li> Try to connect to the listed networks in the Client Configuration
          <li> When none of the networks are found, set up an Access Point
        </ol>
      </p>

      <form ref="form" autocomplete="off" novalidate="novalidate">
         <hr>
         <h5>Client Configuration</h5>
        <!--
         <div class="app-grid">
          <label for="apDisabled">
            <input type="checkbox" id="" id="apDisabled" ref="apDisabled" placeholder="1" />
            When enabled, GATAS will only attempt to an Access point and will not create anAccess Point by itself. This setting is recommended when using GATAS Connect.
          </label>
        </div> -->
        <br />

        <div class="notice notice--warning">
            <div>
            To make this work with a phone's hotspot:<br/>
              <ul>
                <li>Ensure the hotspot is set to 2.4GHz. For an iPhone that means Maximize Compatibility must be enabled, Android phones usually allow both 2.4 and 5GHz.
                <li>For Security set WPA2 Personal (or WPA3 if supported by your device)
              </ul>
              For iPhone we noticed that you need to have the hotspot page open before an iPhone allows a connection. So after starting GATAS, go to your iPhones hotspat page and wait, it should just connect.
            </div>
        </div>


         <div class="app-grid app-grid--2">
            ${this.clientsIds.map(
      (id) => html`
                <div class="split-row endpoint-row">
                  <div class="split-main">
                    <label for="ssid">
                      SSID ${id}:
                      <input type="text" id="apssid_${id}" ref="apssid_${id}" placeholder="SSID" } />
                    </label>
                    <label for="Password">
                      Password ${id}:
                      <input type="text" id="appassword_${id}" ref="appassword_${id}" placeholder="Password" } />
                    </label>
                  </div>
                </div>
              `,
    )}
          </div>
        <br /><br />
          <h5>Access Point configuration</h5>
        <p>Access point configuration. This AP will be created if GATAS could not connect to any other Access Points.
        </p>
        <div class="split-row">
          <div class="split-main">
            <label for="SSID">
              SSID:
              <input type="text" id="apssid" ref="apssid" placeholder="SSID" />
            </label>
            <label for="Password">
              Password:
              <input type="text" id="appassword" ref="appassword" placeholder="Password" />
            </label>
          </div>
      </div>
      <br />
      <div class="notice notice--warning">
        This access point does not support routing to the internet.
      </div>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("wifiservice-config", WifiServiceConfig);

class ADSBDecoderConfig extends ModuleConfig {
  created() {
    this.filterAltitudes = [0, 500, 1000, 1500, 2000, 2500, 3000, 5000, 10000, 15000, 50000];
    this._initForm(store.getModuleData("ADSBDecoder"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.filterAbove, [
        {
          rule: "required",
        },
      ])
      .addField(this.$refs.filterBelow, [
        {
          rule: "required",
        },
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData("ADSBDecoder", { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs[`filterAbove${data.filterAbove}`].selected = true;
    this.$refs[`filterBelow${data.filterBelow}`].selected = true;
  }

  _getFormData() {
    return {
      filterAbove: this.$refs.filterAbove.value,
      filterBelow: this.$refs.filterBelow.value,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the ADSB Decoder</h4>
      <p>
        This module processes the incoming ADS-B received over TCP, Serial or other methods. This module must be enabled next to a module that receives data.
      </p>
      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section app-grid app-grid--2">
          <label for="filterAbove">
            filterAbove:
            <select ref="filterAbove" required>
              ${this.filterAltitudes.map((item) => html` <option ref="filterAbove${item}">${item}</option>`)}
            </select>
            <small><br />Removes all traffic more than ${this.state.data.filterAbove}ft above your own altitude</small>
          </label>
          <label for="filterBelow">
            filterBelow:
            <select ref="filterBelow" required>
              ${this.filterAltitudes.map((item) => html` <option ref="filterBelow${item}">${item}</option>`)}
            </select>
            <small><br />Removes all traffic more than ${this.state.data.filterBelow}ft below your own altitude</small>
          </label>
        </div>
        <br />
        <div class="notice notice--warning">
          ${html.raw(icon.warning)} Increasing the filter below and filter above settings will increase the load on the system, thus degrading the system's
          performance. Don't set the value too high/low.
        </div>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("adsbdecoder-config", ADSBDecoderConfig);

class Bmp280Config extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("Bmp280"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.compensation, [
        {
          rule: "required",
        },
        {
          rule: "integer",
        },
        {
          rule: "minNumber",
          value: -500,
        },
        {
          rule: "maxNumber",
          value: 500,
        },
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData("Bmp280", { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs.compensation.value = data.compensation;
  }

  _getFormData() {
    return {
      compensation: this.$refs.compensation.value,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the Bmp280 pressure sensor</h4>
      <p>This module reads the Bmp280 pressure sensor and makes the data available to other modules that require them.</p>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section app-grid app-grid--2">
          <label for="compensation">
            Compensation:
            <input type="text" id="compensation" ref="compensation" placeholder="0" } />
          </label>
        </div>
        <div class="notice">Compensation allows you to add an offset to the measured pressure for more accurate readings.</div>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}

customElements.define("bmp280-config", Bmp280Config);

class GDLoverUDPConfig extends ModuleConfig {
  created() {
    this.ports = [0, 1];
    this.ips = [0, 1];
    this._initForm(store.getModuleData("GDLoverUDP")).then((data) => {
      this.$update();
    });
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    this.ports.forEach((port) => {
      validator.addField(this.$refs["port_" + port], portValidation);
    });
    this.ips.forEach((port) => {
      validator.addField(this.$refs["ip_" + port], ipValidation);
      validator.addField(this.$refs["ipport_" + port], portValidation);
    });

    validator.onSuccess((event) => {
      const data = this._getFormData();
      store.updateModuleData("GDLoverUDP", { ...this.copyOfData, ...data }).then(() => {
        this.close();
      });
    });
  }

  _hasValue(item) {
    try {
      return this.$refs[item].value || !this.hasData;
    } catch (e) {
      return false;
    }
  }

  _setFormData(data) {
    this.ports.forEach((portno) => {
      let port = this.$refs["port_" + portno];
      let record = data.defaultPorts[portno];
      if (record) {
        port.value = record;
      } else {
        port.value = "";
      }
    });

    this.ips.forEach((ipno) => {
      let ip = this.$refs["ip_" + ipno];
      let port = this.$refs["ipport_" + ipno];
      let record = data.ips[ipno];
      if (record) {
        ip.value = record.ip;
        port.value = record.port;
      } else {
        ip.value = "";
        port.value = "";
      }
    });
  }

  _getFormData() {
    let ports = Array.from(new Set(this.ports.map((portno) => this.$refs["port_" + portno].value).filter((value) => value)));

    let ips = this.ips
      .map((ipno) => ({
        ip: this.$refs["ip_" + ipno].value,
        port: this.$refs["ipport_" + ipno].value,
      }))
      .filter((value) => value.ip && !value.ip.startsWith("127"));

    return {
      defaultPorts: ports,
      ips: ips,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the GDL over UDP Module</h4>
      <p>
        Flight bags such as SkyDemon, ForeFlight, EasyFVR and others can then determine your location and receive information about other aircraft using this
        protocol.<br /><br />
        <div style="text-decoration: underline">When in Client mode:</div>
        <ul>
          <li>GATAS will send GDL90 messages to the access using the below configured ports
          <li>GATAS will send GDL90 to any of the below configured IP addresses and port.
        </ul>
        <div style="text-decoration: underline">When in Access Point mode:</div>
        <ul>
          <li>GATAS will send GDL90 messages to any of the connected clients using the below configured ports
          <li>GATAS will send GDL90 to any of the below configured IP addresses and port.
        </ul>
        In all cases GATAS will send messages to clients in the same network.
      </p>

      <div class="notice">
        ${html.raw(icon.primary)}
        <div>
          <strong>ForeFlight is discovered automatically.</strong><br />
          When ForeFlight announces its GDL90 port, GATAS starts sending traffic to that port. You do not need to add the ForeFlight IP address or port below.
        </div>
      </div>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section">
          Up to ${this.ports.length} different ports can be configured
          <label class="help-label">
            ${html.raw(icon.help)}
            <p class="app-tooltip">
              Each device connected to GATAS will automatically receive GDL90 packets. By default, these packets are sent on port <b>4000</b>, but up to ${this.ports.length}
              different ports can be configured to accommodate devices that listen on other ports. Default port is <b>4000</b>
              When GATAS is connrect to an Access Point, additional GDL messages will be send to the gateway address. This allows
              you to have for example foreflight on your telephone while GATAS uses your telephone as the accesspoint to GATASConnect
              to Receive online traffic information.
            </p>
          </label>
          <div class="app-grid app-grid--4">
            ${this.ports.map(
      (item) => html`
                <label class="has-sub ${this._hasValue("port_" + item) && "active"}">
                  <sub class="active">Port ${item}</sub>
                  <input type="text" id="port_${item}" ref="port_${item}" onblur="this.parentElement.classList.toggle('active', this.value)" />
                </label>
              `,
    )}
          </div>
        </div>

        <div class="page-section">
          Up to ${this.ips.length} different IPs can be configured
          <label class="help-label">
            ${html.raw(icon.help)}
            <p class="app-tooltip">
              In addition to ports, up to ${this.ips.length} separate IP addresses can be configured.
              This is usually only useful if your system connects to an access point and you have a third device connected to the same accesspoint.
              By default GA/TAAS will send UDP traffic to the gateway in client mode using the configured ports.<br />
              Default port is <b>4000</b>
            </p>
          </label>
          <div class="app-grid app-grid--2">
            ${this.ips.map(
      (item) => html`
                <div class="split-row">
                  <div class="split-main">
                    <label class="has-sub ${this._hasValue("ip_" + item) && "active"}">
                      <sub>IP ${item}</sub>
                      <input type="text" id="ip_${item}" ref="ip_${item}" onblur="this.parentElement.classList.toggle('active', this.value)" />
                    </label>
                  </div>
                  <div class="split-aside endpoint-port">
                    <label class="has-sub ${this._hasValue("ipport_" + item) && "active"}">
                      <sub>Port ${item}</sub>
                      <input type="text" inputmode="numeric" maxlength="5" size="5" id="ipport_${item}" ref="ipport_${item}" onblur="this.parentElement.classList.toggle('active', this.value)" />
                    </label>
                  </div>
                </div>
              `,
    )}
          </div>
        </div>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("gdloverudp-config", GDLoverUDPConfig);

class Dump1090ClientConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("Dump1090Client"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.port, [
        {
          rule: "required",
        },
        ...portValidation,
      ])
      .addField(this.$refs.ip, [
        {
          rule: "required",
          errorMessage: "IPv4 address is required",
        },
        ...ipValidation,
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData("Dump1090Client", { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs.ip.value = data.ip;
    this.$refs.port.value = data.port;
  }

  _getFormData() {
    return {
      ip: this.$refs.ip.value,
      port: this.$refs.port.value,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the Dump1090 Client</h4>
      <p>
        This module enables reading ADS-B data in the format '*8D7C7181215D01A08208204D8BF1;' from an external system like Dump1090 into GATAS (port
        &lt;IP&gt;:30002), processing them as traffic targets. Ensure that the ADSBDecoder is enabled and there is traffic within the filtered ranges above or
        below so you will actually see them.
          <div class="notice notice--warning">
          ${html.raw(icon.warning)} This module requires a restart after modification of the configuration.
        </div>
      </p>
      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section app-grid app-grid--2">
          <label for="ip">
            IP:
            <input type="text" id="ip" ref="ip" placeholder="SSID" } />
          </label>
          <label for="port">
            Port:
            <input type="text" id="port" ref="port" placeholder="4000" } />
          </label>
        </div>
        <br />

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("dump1090client-config", Dump1090ClientConfig);

class SX1262 extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData(this.name));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.offset, [
        {
          rule: "required",
        },
        {
          rule: "integer",
        },
        {
          rule: "minNumber",
          value: -30000,
        },
        {
          rule: "maxNumber",
          value: 30000,
        },
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData(this.name, { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs.offset.value = data.offset;
    this.$refs.txEnabled.checked = data.txEnabled;
  }

  _getFormData() {
    return {
      offset: this.$refs.offset.value,
      txEnabled: this.$refs.txEnabled.checked,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the ${this.name} LoRaWAN module</h4>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section app-grid app-grid--2">
          <label for="offset">
            Offset(Hz)
            <label class="help-label">
              ${html.raw(icon.help)}
              <p class="app-tooltip">
                Adds an additional offset to the frequency for the protocol in this device. To calibrate you can use an existing (correctly calibrated) OGN
                receiver and use its frequency offset. When shown as a negative value, use a positive value in the below input field to offset the frequency.
              </p> </label
            >:
            <input type="text" id="offset" ref="offset" placeholder="0" } />
          </label>
          <label for="txEnabled">
            <label class="help-label">
              TX Enabled ${html.raw(icon.help)}
              <p class="app-tooltip">
                Allows to enable or disable complete transmission of any frames from the radio. Normally you want to have this enabled so others can see you.
              </p> </label
            >:
            <br />
            <input type="checkbox" id="txEnabled" ref="txEnabled" placeholder="1" />
          </label>
        </div>
        <br />

        ${this.buttonArray(html)}
      </form>
    `;
  }
}

class Sx1262_0 extends SX1262 {
  name = "Sx1262_0";
}

class Sx1262_1 extends SX1262 {
  name = "Sx1262_1";
}

customElements.define("sx1262_0-config", Sx1262_0);
customElements.define("sx1262_1-config", Sx1262_1);

class BluetoothConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("Bluetooth"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.localName, [
        {
          rule: "required",
        },
        ...ssidValidation,
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData("Bluetooth", { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    // Get Access Point
    if (data == null || data.localName == null) {
      this.$refs.localName.value = "GaTas";
    } else {
      this.$refs.localName.value = data.localName;
    }
  }

  _getFormData() {
    return {
      localName: this.$refs.localName.value,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the Bluetooth Service</h4>
      <p>
        This module allows for a classic bluetooth service and Bluetooth LE:<br />
        Data sent will be Dataport, thus NMEA which is the exact same data as when connecting to Air Connect.
      </p>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="split-row">
          <label for="localName">
            Device Name:
            <input type="text" id="localName" ref="localName" placeholder="GaTas" />
          </label>
        </div>
        <small>Needs restart of GATAS after changing the name</small>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("bluetooth-config", BluetoothConfig);


class GatasConnectConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("GatasConnect"));
  }

  randomIntFromInterval(min, max) { // min and max included
    return Math.floor(Math.random() * (max - min + 1) + min);
  }
  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.pinCode, [
        {
          rule: "custom",
          validator: (value) => {
            const n = Number(value);
            return Number.isInteger(n) && (n === 0 || (n >= 1000 && n <= 999999));
          },
          errorMessage: "Valid pincode must be 0 (disabled the pincode) or between 1000 and 999999",
        }
      ])
      .onSuccess((event) => {
        const data = { ...this.copyOfData, ...this._getFormData() };
        delete data.output;
        store.updateModuleData("GatasConnect", data).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    //    this.$refs.pinCode.value = data.pinCode !== undefined ? data.pinCode : this.randomIntFromInterval(1000, 999999);
    this.$refs.pinCode.value = data.pinCode !== undefined ? data.pinCode : "0";
    this.$refs.enableGdl90Bridge.checked = data.enableGdl90Bridge === true;
  }

  _getFormData() {
    return {
      pinCode: this.$refs.pinCode.value.trim(),
      enableGdl90Bridge: this.$refs.enableGdl90Bridge.checked,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of GATAS Connect</h4>
      <p>
        GATAS connect enabled to use of an external server to ingest additional traffic data, but requires a mobile connection via WIFI or BlueTooth to operate.
        It fetches traffic around you up to about 100Km.
        You must enable GatasConnectUDP when you want gatas to directly connect to GatasServer. At your option, you can also use the Gatas Companion application
        to have GATAS receive additional traffic over bluetooth.
      </p>

        <div class="notice notice--warning">
          <div>
          The Pin Code is used when you use <a href="https://gatas.vantwisk.nl" target="_blank" rel="noopener noreferrer">GATAS Connect</a> online
          application to connect to your GATAS system to allow to configure your GATAS aircraft you are flying.
          Instead of using the unique GATAS ID which is differcult to remmeber, you can use the Pin Code with your location.
          When using 0 as Pin Code, the Pin Code functionality is disabled for added security if you whish to not use it.<br />
          GDL90 bridge is used together with the mobile application <i>Gatas Companion</i> and when enabled it will allow
          to forward GDL90 messages from GATAS via bluetooth to the mobile application that forwards the GDL90 packets to a local UDP server.
          This is to eliminate any WIFI connection if you whish to your mobile if you whish. You can still let other devices connect to GATAS
          to benefit from all traffic and GPS data.
          </div>
        </div>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="split-row">
            <div class="split-main" style="margin-top:20px;">
              <label for="ip">
                Pin Code:
                <input type="text" id="pinCode" ref="pinCode" placeholder="0" } />
              </label>
            </div>
            <div class="split-main" style="margin-top:20px;" ref="enableGdl90BridgeGroup">
              <label for="enableGdl90Bridge">
                Enable Gdl90 Bridge:
                <input type="checkbox" id="enableGdl90Bridge" ref="enableGdl90Bridge" />
              </label>
            </div>
        </div>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("gatasconnect-config", GatasConnectConfig);

class GatasConnectUDPConfig extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("GatasConnectUDP"));
  }

  randomIntFromInterval(min, max) { // min and max included
    return Math.floor(Math.random() * (max - min + 1) + min);
  }
  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .addField(this.$refs.ip, [
        {
          rule: "required",
          errorMessage: "IPv4 address or domain name is required",
        },
        ...configStringLengthValidator,
      ])
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData("GatasConnectUDP", { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs.ip.value = data.gatasServer.ip;
  }

  _getFormData() {
    return {
      gatasServer: {
        ip: this.$refs.ip.value.trim(),
      }
    };
  }

  render(html) {
    return html`
      <h4>Configuration of GATAS Connect UDP</h4>
      <p>
        GATAS connect enabled to use of an external server to ingest additional traffic data, but requires a mobile connection via WIFI to operate.
        It fetches traffic around you up to about 50Km away.
        <br/><br/>
        To setup GATAS Connect:
        <ul>
          <li>Enter the the IP address of your GATAS Connect service you can use <button class="secondary compact-button" type="button" title="Copy to IP field" style="cursor:pointer" onclick=${() => { this.$refs.ip.value = 'gatas.vantwisk.nl'; }}>gatas.vantwisk.nl</button> which is free to use:
          <li>Then setup your WIFI to connect to your mobile hotspot and ensure to enable 'Client Only' in WifiService.
        </ul>
      </p>

        <div class="notice notice--warning">
          <div>
          The Pin Code is used when you use <a href="https://gatas.vantwisk.nl" target="_blank" rel="noopener noreferrer">GATAS Connect</a> online
          application to connect to your GATAS system to allow to configure your GATAS aircraft you are flying.
          Instead of using the unique GATAS ID which is differcult to remmeber, you can use the Pin Code with your location.
          When using 0 as Pin Code, the Pin Code functionality is disabled for added security if you whish to not use it.
          </div>
        </div>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="split-row">
            <div class="split-main">
              <label for="ip">
                IP Address or domain name of GATAS Server:
                <input type="text" id="ip" ref="ip" />
              </label>
            </div>
        </div>

        ${this.buttonArray(html)}
      </form>
    `;
  }
}
customElements.define("gatasconnectudp-config", GatasConnectUDPConfig);

class AbstractGnss extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData(this.name));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);

    validator
      .onSuccess((event) => {
        const data = this._getFormData();
        store.updateModuleData(this.name, { ...this.copyOfData, ...data }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs.softPPS.checked = data.softPPS;
  }

  _getFormData() {
    return {
      softPPS: this.$refs.softPPS.checked,
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the GNSS module</h4>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section app-grid app-grid--2">
          <label for="softPPS">
            <label class="help-label">
              Software based PPS ${html.raw(icon.help)}
              <p class="app-tooltip">
                When Enable a softwareBased but less accurate approach to get a PPS signal from the GNSS data.
                This is useful when your GNSS module does not have a dedicated PPS output pin. If you have PPS, leave this disabled for best accuracy.
              </p> </label>:
            <br />
            <input type="checkbox" id="softPPS" ref="softPPS" placeholder="1" />
          </label>
        </div>
        <br />

        ${this.buttonArray(html)}
      </form>
    `;
  }
}

class L76B extends AbstractGnss {
  name = "L76B";
}

class UbloxM8N extends AbstractGnss {
  name = "UbloxM8N";
}

class StaticGPS extends ModuleConfig {
  created() {
    this._initForm(store.getModuleData("StaticGPS"));
  }

  mounted() {
    const validator = new JustValidate(this.$refs.form);
    const coordinateValidation = (minimum, maximum) => [
      { rule: "required" },
      {
        rule: "custom",
        validator: (value) => Number.isFinite(Number(value)) && Number(value) >= minimum && Number(value) <= maximum,
        errorMessage: `Enter a number from ${minimum} to ${maximum}`,
      },
    ];

    validator
      .addField(this.$refs.latitude, coordinateValidation(-90, 90))
      .addField(this.$refs.longitude, coordinateValidation(-180, 180))
      .addField(this.$refs.altitude, coordinateValidation(-1000, 20000))
      .addField(this.$refs.ntpServer, [
        {
          rule: "maxLength",
          value: 31,
        },
      ])
      .onSuccess(() => {
        store.updateModuleData("StaticGPS", { ...this.copyOfData, ...this._getFormData() }).then(() => {
          this.close();
        });
      });
  }

  _setFormData(data) {
    this.$refs.latitude.value = data?.latitude ?? "0.0";
    this.$refs.longitude.value = data?.longitude ?? "0.0";
    this.$refs.altitude.value = data?.altitude ?? "0.0";
    this.$refs.ntpServer.value = data?.ntpServer ?? "time.cloudflare.com";
  }

  _getFormData() {
    return {
      latitude: this.$refs.latitude.value,
      longitude: this.$refs.longitude.value,
      altitude: this.$refs.altitude.value,
      ntpServer: this.$refs.ntpServer.value.trim(),
    };
  }

  render(html) {
    return html`
      <h4>Configuration of the Static GPS</h4>
      <p>
        Generates a fixed ownship position at zero speed with a north track. NMEA data is sent twice per second. Internet time is synchronized when a Wi-Fi
        client connection is available.
      </p>

      <form ref="form" autocomplete="off" novalidate="novalidate">
        <div class="page-section app-grid app-grid--2">
          <label for="staticGpsLatitude">
            Latitude (decimal degrees)
            <input type="number" step="any" id="staticGpsLatitude" ref="latitude" placeholder="0.0" />
          </label>
          <label for="staticGpsLongitude">
            Longitude (decimal degrees)
            <input type="number" step="any" id="staticGpsLongitude" ref="longitude" placeholder="0.0" />
          </label>
          <label for="staticGpsAltitude">
            Altitude MSL (m)
            <input type="number" step="any" id="staticGpsAltitude" ref="altitude" placeholder="0.0" />
          </label>
          <label for="staticGpsNtpServer">
            Time server
            <input type="text" maxlength="31" id="staticGpsNtpServer" ref="ntpServer" placeholder="time.cloudflare.com" />
          </label>
        </div>
        <div class="notice notice--warning">
          Enable only one GPS module at a time. Static GPS waits for valid network time before advertising a valid fix.
        </div>
        <br />
        ${this.buttonArray(html)}
      </form>
    `;
  }
}

customElements.define("l76b-config", L76B);
customElements.define("ubloxm8n-config", UbloxM8N);
customElements.define("staticgps-config", StaticGPS);

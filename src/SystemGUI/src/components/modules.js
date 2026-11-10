import { El } from "@frameable/el";
import "./monitormodule";
import store from "./store";
import { icon } from "./utils";

const MODULE_NOT_AVAILABLE = 13;

class GaTasModules extends El {
  created() {
    this.state = this.$observable({
      modules: [],
      whatToShow: "modules",
      selectedModule: 0,
      enabled: [],
    });

    // Modules we want to hide because they where never tested, or don't provide any usefull information now
    this.hide = ["Idle", "AceSpi_0", "AceSpi_1", "SerialADSB"];

    // All modules that can be monitored (Sometime we should automate this by reading this from the Microcontroller)
    this.monitorable = [
      "ADSL",
      "Flarm",
      "GDLoverUDP",
      "Gdl90Service",
      "GpsDecoder",
      "Ogn1",
      "Fanet",
      "PicoRtc",
      "RadioTunerTx",
      "RadioTunerRx",
      "RxDataFrameQueue",
      "Sx1262_0",
      "Sx1262_1",
      "Lr2021_0",
      "Lr2021_1",
      "UbloxM8N",
      "L76B",
      "StaticGPS",
      "ADSBDecoder",
      "Dump1090Client",
      "Bmp280",
      "Config",
      "AircraftTracker",
      "DataPort",
      "AirConnect",
      "Bluetooth",
      "Webserver",
      "GatasConnect",
      "GatasConnectUDP",
    ];

    this.configurable = [
      "AircraftTracker",
      "Gdl90Service",
      "DataPort",
      "L76B",
      "UbloxM8N",
      "StaticGPS",
      "WifiService",
      "ADSBDecoder",
      "GDLoverUDP",
      "Dump1090Client",
      "Bmp280",
      "Sx1262_0",
      "Sx1262_1",
      "Lr2021_0",
      "Lr2021_1",
      "Bluetooth",
      "GatasConnect",
      "GatasConnectUDP"
    ];

    this.enablers = [
      "ADSBDecoder",
      "Ogn1",
      "Flarm",
      "Fanet",
      "GDLoverUDP",
      "Gdl90Service",
      "Dump1090Client",
      "Bmp280",
      "ADSL",
      "SerialADSB",
      "Sx1262_0",
      "Sx1262_1",
      "Lr2021_0",
      "Lr2021_1",
      "UbloxM8N",
      "L76B",
      "StaticGPS",
      "RadioTunerRx",
      "RadioTunerTx",
      "DataPort",
      "AirConnect",
      "GatasConnect",
      "GatasConnectUDP",
      "GpsDecoder",
    ];
    this.info = {
      WifiService: (html) =>
        html`Provides both Access Point and Client modes, allowing GaTas to connect to or create a network. It can connect to multiple networks based on
        availability.`,
      AircraftTracker: (html) =>
        html`Tracks all received aircraft and updates their positions when no new data is received. Sends a 1-second heartbeat with updated positions for each
        aircraft for a configurable duration.`,
      Webserver: (html) => html`Hosts this page and facilitates configuration changes.`,
      DataPort: (html) =>
        html`Generates NMEA sentences compatible with DataPort, enabling EFBs like SkyDemon to receive traffic and ownship information via Bluetooth or
        AirConnect.`,
      AirConnect: (html) =>
        html`Sends all DataPort messages over TCP. Prefer Bluetooth if supported by your EFB. Listens on port 2000 and sends data upon connection. Note:
        ForeFlight may require proprietary port negotiation.`,
      Bluetooth: (html) =>
        html`Transmits all DataPort messages over Bluetooth, providing NMEA datastreams to external devices. EFBs like SkyDemon can connect to GaTas via
        Bluetooth.`,
      Gdl90Service: (html) => html`Generates GDL90 messages. Requires a module like GDLoverUDP to receive them on external devices.`,
      GDLoverUDP: (html) =>
        html`Transmits GDL90 messages over UDP to external devices. ForeFlight clients are discovered automatically, and their advertised GDL90 port is used
        without manual client configuration.`,
      GatasConnect: (html) => html`Generates COBS-framed GATAS Connect traffic and routes it to the configured transport.`,
      GatasConnectUDP: (html) => html`Receives and transmits GATAS Connect COBS payloads over UDP.`,
      Flarm: (html) => html`Sends and receives Flarm protocol messages.`,
      Ogn1: (html) => html`Sends and receives OGN protocol messages.`,
      ADSL: (html) => html`Sends and receives ADS-L protocol messages.`,
      ADSBDecoder: (html) => html`Receives ADS-B (extended squitter) messages. Requires an input module like SerialADSB or Dump1090Client.`,
      SerialADSB: (html) => html`Receives ADS-B messages from hardware like the GNS5892. Requires an ADSB Decoder to process messages.`,
      Dump1090Client: (html) => html`Receives ADS-B messages from Dump1090. Requires an ADSB Decoder to process messages.`,
      Bmp280: (html) => html`Reads atmospheric pressure using the Bmp280 hardware.`,
      AceSpi_0: (html) => html`Core module for controlling SPI access between different modules.`,
      AceSpi_1: (html) => html`Core module for controlling SPI access between different modules.`,
      Config: (html) => html`Core module for receiving and storing configurations.`,
      GpsDecoder: (html) => html`Core module for decoding GPS NMEA messages.`,
      UbloxM8N: (html) => html`Configures uBlox GPS devices`,
      L76B: (html) => html`Configures L76B GPS devices`,
      StaticGPS: (html) => html`Generates a configured fixed GPS position and synchronizes UTC time over the network. No GPS hardware is required.`,
      PicoRtc: (html) => html`Reads GPS messages and handles accurate time tracking for various protocols.`,
      Sx1262_0: (html) => html`Radio module 1. Sends and receives ADS-L, OGN, and Flarm protocols.`,
      Sx1262_1: (html) => html`Radio module 2. Sends and receives ADS-L, OGN, and Flarm protocols.`,
      Lr2021_0: (html) => html`Radio module 1. Sends and receives ADS-L, OGN, and Flarm protocols.`,
      Lr2021_1: (html) => html`Radio module 2. Sends and receives ADS-L, OGN, and Flarm protocols.`,
      RadioTunerRx: (html) => html`Manages timings for receiving multiple protocols over one or more radios (Flarm, OGN, and ADS-L).`,
      RadioTunerTx: (html) => html`Manages sending regular position messages over different protocols like Flarm, OGN, and ADS-L.`,
      RxDataFrameQueue: (html) =>
        html`Receives the RAW dataframes from a transceiver and prepares to send them to the various protocols. This will free up the transceiver to do other
        work.`,
    };
  }

  mounted() {
    this._running = false;
    this._newHwIdx = 0;
    this._syncRouteFromLocation = () => {
      const [page, view, encodedModule] = window.location.hash.slice(1).split("/");
      if (page !== "modules") {
        store.state.configurationEditorOpen = false;
        return;
      }

      const module = encodedModule ? decodeURIComponent(encodedModule) : "";
      if (view === "monitor" && this.monitorable.includes(module)) {
        this.state.selectedModule = module;
        this.state.whatToShow = "monitor";
      } else if (view === "configure" && this.configurable.includes(module)) {
        this.state.selectedModule = module;
        this.state.whatToShow = "configure";
      } else {
        this.state.selectedModule = 0;
        this.state.whatToShow = "modules";
      }
      store.state.configurationEditorOpen = this.state.whatToShow === "configure";
    };
    this._onNavigate = (event) => {
      if (event.detail?.page === "modules") {
        this._syncRouteFromLocation();
      }
    };
    window.addEventListener("gatas:navigate", this._onNavigate);
    window.addEventListener("hashchange", this._syncRouteFromLocation);
    this._syncRouteFromLocation();
    this._fetchData();
  }

  unmounted() {
    this._running = false;
    clearTimeout(this.timer);
    store.state.configurationEditorOpen = false;
    window.removeEventListener("gatas:navigate", this._onNavigate);
    window.removeEventListener("hashchange", this._syncRouteFromLocation);
  }

  _postConstructToString(value) {
    const errorMap = {
      0: "Never Loaded",
      1: "Ok",
      2: "PostConstruct failed",
      3: "Memory error",
      4: "A dependency was not found",
      5: "xQueue error",
      6: "Task Error",
      7: "Hardware not found",
      8: "Hardware error",
      9: "Network error",
      10: "Configuration error",
      11: "Timer error",
      12: "Mutex error",
      13: "Not Available",
      14: "Spinlock not available",
    };
    return errorMap[value] || "Unknown error";
  }

  _fetchData() {
    store
      .fetch("/api/Webserver.json")
      .then((data) => {
        this.state.modules.length = 0;
        for (var prop in data.modules) {
          this.state.modules.push({ name: prop, ...data.modules[prop] });
        }
      })
      .catch((e) => {
        console.log("Error" + e);
        this.state.modules.length = 0;
      })
      .then((data) => {
        return store.getModuleData("modules").then((data) => {
          this.state.enabled = (data ?? "").split(",");
          return data;
        });
      })
      .finally(() => {
        this.$update();
        if (this._running) {
          this.timer = setTimeout(() => {
            this._fetchData();
          }, 5000);
        }
      });
  }

  _monitorModule(module) {
    window.location.hash = `modules/monitor/${encodeURIComponent(module)}`;
  }

  _configureModule(module) {
    window.location.hash = `modules/configure/${encodeURIComponent(module)}`;
  }

  _showModules() {
    window.location.hash = "modules";
  }

  _toggleModule(moduleName) {
    const index = this.state.enabled.indexOf(moduleName);
    if (index !== -1) {
      this.state.enabled.splice(index, 1);
    } else {
      this.state.enabled.push(moduleName);
    }

    store.updateModuleData("modules", this.state.enabled.join(",")).then((data) => {
      return data;
    });
  }

  _showConfigureModule(html) {
    let module = this.state.selectedModule;
    return html`
      <${module}-config key="${module}-config" close=${() => this._showModules()}></${module}-config>
    `;
  }

  _showModuleStatus(html) {
    return html`
      <monitor-module key="config" selected=${this.state.selectedModule}></monitor-module>
      <button class="secondary" onclick=${() => this._showModules()}>Back to modules</button>
    `;
  }

  _row(html, item) {
    if (item.poststatus == MODULE_NOT_AVAILABLE || this.hide.includes(item.name)) {
      return html``;
    } else {
      let monitorBtn =
        item.poststatus == 1 && this.monitorable.includes(item.name)
          ? html`<button
              class="secondary compact-button"
              title="Monitor ${item.name}"
              aria-label="Monitor ${item.name}"
              onclick=${() => this._monitorModule(item.name)}
            >
              ${html.raw(icon.monitor)}
            </button>`
          : "";
      let configureBtn = this.configurable.includes(item.name)
        ? html`<button
            class="secondary compact-button"
            title="Configure ${item.name}"
            aria-label="Configure ${item.name}"
            onclick=${() => this._configureModule(item.name)}
          >
            ⚙
          </button>`
        : "";

      let toggleBtn = this.state.enabled.includes(item.name)
        ? html`<button
            class="success compact-button"
            title="Disable ${item.name}"
            aria-label="Disable ${item.name}"
            onclick=${() => this._toggleModule(item.name)}
          >
            ✓
          </button>`
        : html`<button
            class="danger compact-button"
            title="Enable ${item.name}"
            aria-label="Enable ${item.name}"
            onclick=${() => this._toggleModule(item.name)}
          >
            ×
          </button>`;

      let enabledBtn = this.enablers.includes(item.name) ? toggleBtn : "";

      let info = "";
      if (this.info[item.name]) {
        info = html` <span class="icon-button" tabindex="0" aria-label="About ${item.name}">
          ${html.raw(icon.help)}
          <span class="app-tooltip" role="tooltip"> ${this.info[item.name](html)} </span>
        </span>`;
      }

      return html` <tr>
        <th scope="row">${item.name} ${info}</th>
        <td>${this._postConstructToString(item.poststatus)}</td>
        <td><div class="module-actions">${enabledBtn} ${configureBtn} ${monitorBtn}</div></td>
      </tr>`;
    }
  }

  _filteredItems() {
    return this.state.modules.filter((i) => !i.name.startsWith("_"));
  }

  _showModuleOverview(html) {
    let items = this._filteredItems();
    return html`
      <section class="page-section">
        <header>
          <h2>Modules</h2>
          <p>Monitor, configure, and enable GATAS services.</p>
        </header>
        <div class="table-wrap">
          <table class="data-table module-table">
            <tbody>
              ${items.map((item) => html` ${this._row(html, item)} `)}
            </tbody>
          </table>
        </div>
      </section>
    `;
  }

  render(html) {
    let pageContent;
    switch (this.state.whatToShow) {
      case "modules":
        pageContent = this._showModuleOverview(html);
        break;
      case "monitor":
        pageContent = this._showModuleStatus(html);
        break;
      case "configure":
        pageContent = this._showConfigureModule(html);
        break;
      default:
        pageContent = "Not available";
    }

    return pageContent;
  }
}
customElements.define("gatas-modules", GaTasModules);

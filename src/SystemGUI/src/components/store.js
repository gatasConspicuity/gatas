import { El } from "@frameable/el";

let instance;

export class GaTasStore {
  availableHardware = [{ name: "-", hardware: "-" }, { name: "GATAS Custom", hardware: "PICO_2RADIO" }, { name: "GATAS Pulse", hardware: "WAVESHARE_LITE" }, { name: "GATAS Dual LR2021", hardware: "GATAS_DUAL_LR2021" }];

  constructor() {
    if (instance) {
      throw new Error("New instance cannot be created!!");
    }

    // Polyfill for older iphones where AbortSignal.timeout is not available
    if (!AbortSignal.timeout) {
      AbortSignal.timeout = function (ms) {
        const controller = new AbortController();
        setTimeout(() => controller.abort(), ms);
        return controller.signal;
      };
    }

    instance = this;

    this.state = El.observable({
      page: "", // Default startup page
      currentModule: "",
      connected: false,
      aircraftId: "",
      aircraftsObj: {},
      aircrafts: [],
      numberOfAircrafts: 0,
      configModified: false,
      configurationEditorOpen: false,
      hardware: {},
      hardwareName: "",
      gatasId: ""
    });
  }

  _deepTrim(value) {
    if (typeof value === "string") {
      return value.trim();
    }

    if (Array.isArray(value)) {
      return value.map(v => this._deepTrim(v));
    }

    if (value !== null && typeof value === "object") {
      return Object.fromEntries(
        Object.entries(value).map(([k, v]) => [k, this._deepTrim(v)])
      );
    }

    return value;
  }

  /**
   * Update store aircraft array
   *
   */
  _updateAircraftArray() {
    this.state.aircrafts.length = 0;
    for (var prop in this.state.aircraftsObj) {
      this.state.aircrafts.push({
        name: prop,
        ...this.state.aircraftsObj[prop],
      });
    }
    this.state.numberOfAircrafts = this.state.aircrafts.length;
  }

  /**
   * Initialize the store
   *
   * @returns
   */
  async init() {
    // Fetch config.json
    const config = await this.fetch("/api/Config.json");
    this.state.gatasId = (config.gatasId).toString(16).toUpperCase();

    // Fetch current aircraft and hw config
    const configData = await this.fetch("/api/Config/config.json");
    this.state.aircraftId = configData.aircraftId;
    this.state.configModified = configData._dirty;

    // Fetch hardware.json
    const hardwareData = await this.fetch("/api/Config/hardware.json");
    Object.assign(this.state.hardware, hardwareData);
    this.state.hardwareName = this.availableHardware.find(d => d.hardware === hardwareData.type)?.name;

    // Fetch aircraft.json
    const aircraftData = await this.fetch("/api/Config/aircraft.json");
    Object.assign(this.state.aircraftsObj, aircraftData);
    this._updateAircraftArray();

    // Validate if the configured aircraft exists
    if (aircraftData[this.state.aircraftId] === undefined) {
      if (this.state.aircrafts.length > 0) {
        await this.setDefaultAirCraftId(this.state.aircrafts[0].callSign);
      } else {
        this.state.aircraftId = "";
      }
    }
  }

  /**
   * Delete an aircraft by ID from the aircrafts path
   *
   * @param {*} aircraftId
   * @returns
   */
  deleteAircraft(aircraftId) {
    return this.fetch(`/api/Config/aircraft/${aircraftId}.json`, {
      method: "POST",
      headers: {
        "X-Method": "DELETE",
      },
      body: "{}",
    }).then((data) => {
      delete this.state.aircraftsObj[aircraftId];
      this._updateAircraftArray();
      return this.init();
    });
  }

  /**
   * Update aircraft in the aircrafts path
   *
   * @param {*} aircraft
   * @returns
   */
  updateAircraft(aircraft) {
    return this.fetch(`/api/Config/aircraft/${aircraft.callSign}.json`, {
      method: "POST",
      headers: {
        "X-Method": "POST",
      },
      body: JSON.stringify(aircraft),
    }).then((data) => {
      this.state.aircraftsObj[aircraft.callSign] = {};
      Object.assign(this.state.aircraftsObj[aircraft.callSign], aircraft);
      this._updateAircraftArray();
      return data;
    });
  }

  /**
   * Update the type of board this is running on
   * @param {*} type
   * @returns
   */
  updateHardware(typeIdx) {
    const type = this.availableHardware[typeIdx].hardware;
    this.state.hardwareName = this.availableHardware[typeIdx].name;
    return this.fetch(`/api/Config/hardware.json`, {
      method: "POST",
      headers: {
        "X-Method": "POST",
      },
      body: JSON.stringify({ type }),
    }).then((data) => {
      this.state.hardware.type = type;
      return data;
    });
  }

  /**
   * Set the default aircraft ID in the config path
   *
   * @param {aircraftId of the aircraft that needs to be set as default} aircraftId
   * @returns
   */
  setDefaultAirCraftId(aircraftId) {
    const url = "/api/Config/config.json";
    return store
      .fetch(url)
      .then((data) => {
        data.aircraftId = aircraftId;
        return this.fetch(url, {
          method: "POST",
          body: JSON.stringify(data),
        });
      })
      .then((data) => {
        this.state.aircraftId = aircraftId;
        return data;
      });
  }

  getModuleData(moduleName) {
    return store.fetch(`/api/Config/${moduleName}.json`).then((data) => {
      // null means configuration did not exist.
      if (data == null) {
        return {};
      } else {
        return data;
      }
    });
  }

  updateModuleData(moduleName, data) {
    return store.fetch(`/api/Config/${moduleName}.json`, {
      method: "POST",
      body: JSON.stringify(this._deepTrim(data)),
    });
  }
  storeInBRModuleData() {
    return store.fetch(`/api/Config/SaveBr.json`, {
      method: "POST",
      body: {},
    });
  }
  restart() {
    return store.fetch(`/api/Config/Restart.json`, {
      method: "POST",
      body: {},
    });
  }

  usbBoot() {
    return store.fetch(`/api/Config/UsbBoot.json`, {
      method: "POST",
      body: {},
    });
  }

  startAp() {
    return store.fetch(`/api/WifiService/startAp.json`, {
      method: "POST",
      body: {},
    });
  }

  /**
   * Fetch operation with default handler
   *
   * @param {} path
   * @param {*} requestOptions
   * @returns
   */
  fetch(path, requestOptions) {
    return fetch(path, {
      ...requestOptions,
      signal: AbortSignal.timeout(5500),
    })
      .then((response) => {
        if (!response.ok) {
          throw new Error(`Device rejected ${requestOptions?.method ?? "GET"} ${path} (${response.status})`);
        }
        if (path.includes("SaveBr.json")) {
          this.state.configModified = false;
        } else if (["POST", "DELETE", "PATCH"].includes(requestOptions?.method)) {
          this.state.configModified = true;
        }
        this.state.connected = true;
        return response.json();
      })
      .then((data) => {
        return new Promise((resolve) => setTimeout(() => resolve(data), 10)); // Delay to not overload the PICO
      })
      .catch((e) => {
        this.state.connected = false;
        throw new Error(e);
      });
  }
}

let store = Object.freeze(new GaTasStore());
export default store;

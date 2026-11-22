import { El } from "@frameable/el";
import store from "./store";

class ActionButtons extends El {
    created() {
        this.state = this.$observable({
            restartDlg: false,
            eraseBrDlg: false,
            erasingBr: false,
            changeHwDlg: false,
            startApDlg: false,
            apStarted: false
        });
    }

    _restart() {
      store.restart();
      this.state.restartDlg = false;
    }

    async _eraseBr() {
        this.state.erasingBr = true;
        try {
            await store.eraseBr();
        } finally {
            this.state.erasingBr = false;
            this.state.eraseBrDlg = false;
        }
    }

    _usbBoot() {
      store.usbBoot();
      this.state.usbBootDlg = false;
    }

    _startAp() {
      this.state.apStarted = true;
      store.startAp();
    }

    _restartButton(html) {
        return html`<button class="secondary" onclick=${() => (this.state.restartDlg = true)}>Restart device</button>
          ${this.state.restartDlg ? this._restartAreYouSureDlg(html) : ""} ${this.state.usbBootDlg ? this._usbBootDlgAreYouSureDlg(html) : ""}
    `;
    }

    _usbBootButton(html) {
        return html`<button class="secondary" onclick=${() => (this.state.usbBootDlg = true)}>Upload firmware</button>`;
    }

    _eraseBrButton(html) {
        return html`<button class="danger" onclick=${() => (this.state.eraseBrDlg = true)}>Erase BR</button>
          ${this.state.eraseBrDlg ? this._eraseBrDialog(html) : ""}`;
    }

    _eraseBrDialog(html) {
        return html`<div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="erase-br-title">
      <div class="app-modal__surface">
        <article>
          <header>
            <h4 id="erase-br-title">Erase BR?</h4>
          </header>
          <div class="app-modal__body">
            <p>This erases the configuration from flash and RAM. Restart the device to load the default configuration.</p>
          </div>
          <footer class="app-modal__actions">
            <button type="button" class="danger" onclick=${this._eraseBr} ${this.state.erasingBr ? "disabled" : ""}>Erase BR</button>
            <button type="button" class="secondary" onclick=${() => (this.state.eraseBrDlg = false)} ${this.state.erasingBr ? "disabled" : ""}>Cancel</button>
          </footer>
        </article>
      </div>
    </div>`;
    }

    _restartAreYouSureDlg(html) {
        return html` <div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="restart-title">
      <div class="app-modal__surface">
        <article>
          <header>
            <h4 id="restart-title">Restart GATAS?</h4>
          </header>
          <div class="app-modal__body">
            <p>
              The connection will be temporarily disconnected. Any unsaved data will be available after restart.<br />
              Are you sure?
            </p>
          </div>
          <footer class="app-modal__actions">
            <button type="button" class="danger" onclick=${this._restart}>Restart</button>
            <button type="button" class="secondary" onclick=${() => (this.state.restartDlg = false)}>Cancel</button>
          </footer>
        </article>
      </div>
    </div>`;
    }

    _usbBootDlgAreYouSureDlg(html) {
        return html` <div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="firmware-title">
      <div class="app-modal__surface">
        <article>
          <header>
            <h4 id="firmware-title">Start firmware mode?</h4>
          </header>
          <div class="app-modal__body">
            <p>
              To update GaTas, make sure it is connected to your computer with a USB cable through the <strong>Microcontroller port</strong> (the charge port
              won’t work for this step).
            </p>
            <p>
              When GaTas restarts, it will show up as a new drive on your computer. Once you see the drive, simply drag and drop the
              <strong>GaTas.uf2</strong> file onto it. After a moment, the device will restart automatically, and GaTas will be ready to use again.
            </p>
          </div>
          <footer class="app-modal__actions">
            <button type="button" class="danger" onclick=${this._usbBoot}>Start firmware mode</button>
            <button type="button" class="secondary" onclick=${() => (this.state.usbBootDlg = false)}>Cancel</button>
          </footer>
        </article>
      </div>
    </div>`;
    }

    _changeHwButton(html) {
        return html`<button class="secondary" onclick=${() => (this.state.changeHwDlg = true)}>Board: ${store.state.hardwareName}</button>
          ${this.state.changeHwDlg === true ? this._changeHardwareDialog(html) : ""}
    `;
    }

    async _hardwareUpdatedConfirm(e) {
        if (this._selectedHwIdx > 0) {
            this.state.changeHwDlg = true;
            await store.updateHardware(this._selectedHwIdx);
            this._selectedHwIdx = 0;
            store.restart();
        }
        this.state.changeHwDlg = false;
    }

    _changeHardwareDialog(html) {
        return html` <div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="hardware-title">
      <div class="app-modal__surface">
        <article>
          <header>
          <h4 id="hardware-title">Change hardware model?</h4>
          </header>
          <div class="app-modal__body">
            <p>
              This will change the type of board that GaTas is running on. 
              After changing, the connection will be temporarily disconnected.
              Any unsaved data will be available after restart.<br />
            </p>

            <select onchange=${e => this._selectedHwIdx = e.currentTarget.selectedIndex}>
              ${store.availableHardware.map(
            (item) => html`<option ${item.hardware === store.state?.hardware?.type ? "selected" : ""} value="${item.hardware}">${item.name}</option>`,
        )}
            </select>

          </div>
          <footer class="app-modal__actions">
            <button type="button" class="danger" onclick=${this._hardwareUpdatedConfirm}>Change and restart</button>
            <button type="button" class="secondary" onclick=${() => (this.state.changeHwDlg = false)}>Cancel</button>
          </footer>
        </article>
      </div>
    </div>`;
    }

    _startApButton(html) {
        return html`<button class="secondary" onclick=${() => (this.state.startApDlg = true)}>Start access point</button>
          ${this.state.startApDlg === true ? this._startApDialog(html) : ""}
    `;
    }

    _startApDialog(html) {
      const footer = !this.state.apStarted?html`
        <footer class="app-modal__actions">
          <button type="button" class="danger" onclick=${this._startAp}>Start access point</button>
          <button type="button" class="secondary" onclick=${() => (this.state.startApDlg = false)}>Cancel</button>
        </footer>
        `:html`
          <footer>
            <div class="notice">
              <p>
              <strong>Access Point mode has been initiated.<br /></strong>
              Please connect to the GATAS Access Point to continue the setup.
              </p>
            </div>
          </footer>
        `;

      return html` <div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="access-point-title">
      <div class="app-modal__surface">
        <article>
          <header>
            <h4 id="access-point-title">Start access point</h4>
          </header>
          <div class="app-modal__body">
            <p>
              If GATAS is currently in client mode, it will exit that mode and start the GATAS Access Point.
            </p>
            <p>
              After GATAS restarts, connect your device to the <strong>GATAS</strong> Wi-Fi network. (Or how you named it)
              Then open your browser and go to <strong><a href="http://192.168.1.1">http://192.168.1.1</a></strong> to access the user interface.
              <br /><br />
              Once configured restart the device to get back into normal WIFI mode. 
            </p>
          </div>
          ${footer}
        </article>
      </div>
    </div>`;
    }

    render(html) {
        return html`
      <section class="page-section">
        <header>
          <h2>Device actions</h2>
          <p>Maintenance and connectivity controls for this GATAS device.</p>
        </header>
        <div class="action-grid">
          <div>${this._restartButton(html)}</div>
          <div>${this._usbBootButton(html)}</div>
          <div>${this._changeHwButton(html)}</div>
          <div>${this._startApButton(html)}</div>
          ${store.state.debug ? html`<div>${this._eraseBrButton(html)}</div>` : ""}
        </div>
      </section>
    `;
    }
}

customElements.define("action-buttons", ActionButtons);

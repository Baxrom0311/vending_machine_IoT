/**
 * Generates the configuration form HTML
 * @param {string} prefix - ID prefix to avoid collisions (e.g., 'serial_' or 'online_')
 */
export function generateBasicConfigForm(prefix = '') {
    const p = prefix;
    return `
    <section class="config-section">
        <h3>Network</h3>
        <div class="form-grid two">
            <div class="form-group">
                <label>WiFi SSID</label>
                <input type="text" id="${p}wifiSsid" placeholder="Network name">
            </div>
            <div class="form-group">
                <label>WiFi Password</label>
                <input type="password" id="${p}wifiPassword" placeholder="Leave blank to keep current">
            </div>
            <div class="form-group">
                <label>MQTT Broker</label>
                <input type="text" id="${p}mqttBroker" placeholder="broker.example.com">
            </div>
            <div class="form-group">
                <label>MQTT Port</label>
                <input type="number" id="${p}mqttPort" value="1883">
            </div>
            <div class="form-group">
                <label>MQTT Username</label>
                <input type="text" id="${p}mqttUsername" placeholder="(optional)">
            </div>
            <div class="form-group">
                <label>MQTT Password</label>
                <input type="password" id="${p}mqttPassword" placeholder="Leave blank to keep current">
            </div>
            <div class="form-group">
                <label>Device ID</label>
                <input type="text" id="${p}deviceId" placeholder="VendingMachine_001">
            </div>
        </div>
    </section>
    `;
}

export function generateExtraConfigForm(prefix = '') {
    const p = prefix;
    return `

    <section class="config-section">
        <h3>Vending</h3>
        <div class="form-grid three">
            <div class="form-group">
                <label>Price per Liter (so'm)</label>
                <input type="number" id="${p}pricePerLiter" value="1000">
            </div>
            <div class="form-group">
                <label>Session Timeout (sec)</label>
                <input type="number" id="${p}sessionTimeout" value="300">
            </div>
            <div class="form-group">
                <label>Relay Polarity</label>
                <div class="toggle-row">
                    <input type="checkbox" id="${p}relayActiveHigh" checked>
                    <span>Active HIGH</span>
                </div>
            </div>
            <div class="form-group">
                <label>Enable Free Water</label>
                <div class="toggle-row">
                    <input type="checkbox" id="${p}enableFreeWater" checked>
                    <span>Enabled</span>
                </div>
            </div>
            <div class="form-group">
                <label>Free Water Cooldown (sec)</label>
                <input type="number" id="${p}freeWaterCooldown" value="180">
            </div>
            <div class="form-group">
                <label>Free Water Amount (ml)</label>
                <input type="number" id="${p}freeWaterAmount" value="200">
            </div>
        </div>
    </section>

    <section class="config-section">
        <h3>Sensors</h3>
        <div class="form-grid four">
            <div class="form-group">
                <label>Pulses per Liter</label>
                <input type="number" id="${p}pulsesPerLiter" value="450" step="0.1">
            </div>
            <div class="form-group">
                <label>TDS Threshold (ppm)</label>
                <input type="number" id="${p}tdsThreshold" value="100">
            </div>
            <div class="form-group">
                <label>TDS Temperature (°C)</label>
                <input type="number" id="${p}tdsTemperatureC" value="25" step="0.1">
            </div>
            <div class="form-group">
                <label>TDS Calibration Factor</label>
                <input type="number" id="${p}tdsCalibrationFactor" value="0.5" step="0.01">
            </div>
        </div>
    </section>

    <section class="config-section">
        <h3>Cash Acceptor</h3>
        <div class="form-grid three">
            <div class="form-group">
                <label>Pulse Value (so'm)</label>
                <input type="number" id="${p}cashPulseValue" value="1000">
            </div>
            <div class="form-group">
                <label>Pulse Gap (ms)</label>
                <input type="number" id="${p}cashPulseGapMs" value="120">
            </div>
        </div>
    </section>

    <section class="config-section">
        <h3>Security</h3>
        <div class="form-grid two">
            <div class="form-group">
                <label>API Secret (signing)</label>
                <input type="password" id="${p}apiSecret" placeholder="Leave blank to keep current">
            </div>
            <div class="form-group">
                <label>Require Signed MQTT</label>
                <div class="toggle-row">
                    <input type="checkbox" id="${p}requireSigned">
                    <span>Enabled</span>
                </div>
            </div>
            <div class="form-group">
                <label>Allow Remote Network Config</label>
                <div class="toggle-row">
                    <input type="checkbox" id="${p}allowRemoteNetworkConfig" checked>
                    <span>Enabled</span>
                </div>
            </div>
            <div class="form-group">
                <label>Group ID (Fleet Management)</label>
                <input type="text" id="${p}groupId" placeholder="e.g., building1, floor2" maxlength="31">
                <small>Leave blank for no group</small>
            </div>
        </div>
    </section>

    <section class="config-section">
        <h3>Intervals</h3>
        <div class="form-grid four">
            <div class="form-group">
                <label>Payment Check (ms)</label>
                <input type="number" id="${p}paymentCheckInterval" value="2000">
            </div>
            <div class="form-group">
                <label>Display Update (ms)</label>
                <input type="number" id="${p}displayUpdateInterval" value="100">
            </div>
            <div class="form-group">
                <label>TDS Check (ms)</label>
                <input type="number" id="${p}tdsCheckInterval" value="5000">
            </div>
            <div class="form-group">
                <label>Heartbeat (ms)</label>
                <input type="number" id="${p}heartbeatInterval" value="30000">
            </div>
        </div>
    </section>

    <section class="config-section">
        <h3>Power Management (Reserved)</h3>
        <small style="color: var(--text-muted); display:block; margin-bottom: 10px;">
            Not supported in current firmware (shown for future use).
        </small>
        <div class="form-grid three">
            <div class="form-group">
                <label>Enable Power Save</label>
                <div class="toggle-row">
                    <input type="checkbox" id="${p}enablePowerSave" checked disabled>
                    <span>Enabled</span>
                </div>
            </div>
            <div class="form-group">
                <label>Deep Sleep Start (Hour 0-23)</label>
                <input type="number" id="${p}deepSleepStartHour" value="1" min="0" max="23" disabled>
            </div>
            <div class="form-group">
                <label>Deep Sleep End (Hour 0-23)</label>
                <input type="number" id="${p}deepSleepEndHour" value="6" min="0" max="23" disabled>
            </div>
        </div>
    </section>
    `;
}

// Backward compatible: keep full form generator (basic + extra)
export function generateConfigForm(prefix = '') {
    return generateBasicConfigForm(prefix) + generateExtraConfigForm(prefix);
}

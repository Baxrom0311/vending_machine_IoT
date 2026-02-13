import { logToElement, delay, maskCommandForLog, validateConfigInputs } from './utils.js';

export function setupSerial(prefix) {
    const p = prefix; // 'serial_'
    let isConnected = false;
    let currentPort = null;
    let isFlashing = false;
    const loadedState = {
        hasReadConfig: false,
        wifiSsid: '',
        mqttUsername: '',
    };

    // DOM Elements
    const portSelect = document.getElementById('serialPortSelect');
    const refreshBtn = document.getElementById('refreshPortsBtn');
    const connectBtn = document.getElementById('connectSerialBtn');
    const monitorOutput = 'serialMonitorOutput'; // ID string
    const commandInput = document.getElementById('serialCommandInput');
    const sendBtn = document.getElementById('serialSendBtn');

    // Config buttons (Basic / Extra)
    const loadBasicBtn = document.getElementById(p + 'loadBasicConfigBtn');
    const saveBasicBtn = document.getElementById(p + 'saveBasicConfigBtn');
    const loadExtraBtn = document.getElementById(p + 'loadExtraConfigBtn');
    const saveExtraBtn = document.getElementById(p + 'saveExtraConfigBtn');

    // Firmware elements
    const firmwareContainer = document.getElementById('serialFirmwareContainer');

    // Inject Firmware UI locally since it's specific to serial
    firmwareContainer.innerHTML = `
        <div class="card">
            <h3>Firmware Update via USB</h3>
            <div class="form-group">
                <label>Firmware File (.bin):</label>
                <div class="file-row">
                    <input type="text" id="serialFirmwarePath" readonly placeholder="Select .bin file">
                    <button class="btn btn-secondary" id="serialBrowseBtn">Browse</button>
                </div>
            </div>
            <div class="form-group">
                <label>Baud Rate:</label>
                <select id="serialFlashBaud">
                    <option value="460800" selected>460800</option>
                    <option value="921600">921600</option>
                    <option value="115200">115200</option>
                </select>
            </div>
            <div class="form-group">
                <label>Flash Type:</label>
                <select id="serialFlashType">
                    <option value="app" selected>App Only (0x10000)</option>
                    <option value="full">Full Firmware (0x0000)</option>
                </select>
            </div>
            <button class="btn btn-warning" id="serialFlashBtn" disabled>⚡ Flash Firmware</button>
            
            <div class="progress-container" id="serialFlashProgress" style="display:none">
                <div class="progress-bar-bg">
                    <div id="serialProgressFill" class="progress-fill"></div>
                </div>
                <span id="serialProgressText">0%</span>
            </div>
            <div id="serialFirmwareLog" class="console-output small"></div>
        </div>
    `;

    // Event Listeners
    refreshBtn.addEventListener('click', scanPorts);
    connectBtn.addEventListener('click', toggleConnection);
    sendBtn.addEventListener('click', sendUserCommand);
    commandInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') sendUserCommand();
    });

    loadBasicBtn?.addEventListener('click', loadConfig);
    loadExtraBtn?.addEventListener('click', loadConfig);
    saveBasicBtn?.addEventListener('click', saveBasicConfig);
    saveExtraBtn?.addEventListener('click', saveExtraConfig);

    document.getElementById('serialBrowseBtn').addEventListener('click', browseFirmware);
    document.getElementById('serialFlashBtn').addEventListener('click', flashFirmware);

    // Initial Scan
    scanPorts();

    // Serial Data Handler
    window.electronAPI.onSerialData((data) => {
        logToElement(monitorOutput, data, 'response');
        parseConfigLine(data, p, loadedState);
    });

    window.electronAPI.onSerialError((err) => {
        logToElement(monitorOutput, 'Error: ' + err, 'error');
        setConnectedState(false);
    });

    window.electronAPI.onSerialClosed(() => {
        logToElement(monitorOutput, 'Connection closed', 'error');
        setConnectedState(false);
    });

    // FUNCTIONS

    async function scanPorts() {
        const ports = await window.electronAPI.scanPorts();
        portSelect.innerHTML = '<option value="">Select Port...</option>';
        ports.forEach(port => {
            const opt = document.createElement('option');
            opt.value = port.path;
            opt.textContent = `${port.path} (${port.manufacturer || 'Unknown'})`;
            portSelect.appendChild(opt);
        });
    }

    async function toggleConnection() {
        if (isConnected) {
            await window.electronAPI.disconnectDevice();
            setConnectedState(false);
            loadedState.hasReadConfig = false;
            loadedState.wifiSsid = '';
            loadedState.mqttUsername = '';
        } else {
            const port = portSelect.value;
            if (!port) return alert('Select a port');

            const result = await window.electronAPI.connectDevice(port);
            if (result.success) {
                currentPort = port;
                setConnectedState(true);
                loadedState.hasReadConfig = false;
                loadedState.wifiSsid = '';
                loadedState.mqttUsername = '';
                logToElement(monitorOutput, 'Connected to ' + port, 'response');
                // Auto-load config only for Main controller (Payment has no config interface)
                const mode = localStorage.getItem('ewater_controller_mode') || 'main';
                if (mode !== 'payment') {
                    setTimeout(loadConfig, 500);
                }
            } else {
                logToElement(monitorOutput, 'Connection failed: ' + result.message, 'error');
            }
        }
    }

    function setConnectedState(connected) {
        isConnected = connected;
        connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
        connectBtn.className = connected ? 'btn btn-danger' : 'btn btn-primary';
        portSelect.disabled = connected;
        refreshBtn.disabled = connected;
    }

    async function sendUserCommand() {
        const cmd = commandInput.value.trim();
        if (!cmd) return;
        if (!isConnected) return alert('Not connected');

        logToElement(monitorOutput, '> ' + cmd, 'command');
        await window.electronAPI.sendCommand(cmd);
        commandInput.value = '';
    }

    async function loadConfig() {
        if (!isConnected) return alert('Not connected');
        const mode = localStorage.getItem('ewater_controller_mode') || 'main';
        if (mode === 'payment') return alert('Payment controller has no configurable settings (flash + monitor only).');
        logToElement(monitorOutput, '> GET_CONFIG', 'command');
        await window.electronAPI.sendCommand('GET_CONFIG');
    }

    async function saveBasicConfig() {
        if (!isConnected) return alert('Not connected');
        const mode = localStorage.getItem('ewater_controller_mode') || 'main';
        if (mode === 'payment') return alert('Payment controller has no configurable settings (flash + monitor only).');

        const built = buildBasicConfigCommands(p, loadedState);
        if (built.error) return alert(built.error);
        const commands = built.cmds;
        for (const cmd of commands) {
            logToElement(monitorOutput, '> ' + maskCommandForLog(cmd), 'command');
            await window.electronAPI.sendCommand(cmd);
            await delay(200);
        }

        const applyMode = document.getElementById(p + 'basic_applyMode').value;
        if (applyMode === 'now') {
            await window.electronAPI.sendCommand('APPLY_CONFIG');
        } else {
            await window.electronAPI.sendCommand('RESTART');
        }
        logToElement(monitorOutput, 'Basic config saved!', 'response');
    }

    async function saveExtraConfig() {
        if (!isConnected) return alert('Not connected');
        const mode = localStorage.getItem('ewater_controller_mode') || 'main';
        if (mode === 'payment') return alert('Payment controller has no configurable settings (flash + monitor only).');

        const commands = buildExtraConfigCommands(p);
        for (const cmd of commands) {
            logToElement(monitorOutput, '> ' + maskCommandForLog(cmd), 'command');
            await window.electronAPI.sendCommand(cmd);
            await delay(200);
        }

        const applyMode = document.getElementById(p + 'extra_applyMode').value;
        if (applyMode === 'now') {
            await window.electronAPI.sendCommand('APPLY_CONFIG');
        } else {
            await window.electronAPI.sendCommand('RESTART');
        }
        logToElement(monitorOutput, 'Extra config saved!', 'response');
    }

    async function browseFirmware() {
        const res = await window.electronAPI.selectFirmware();
        if (res.success) {
            document.getElementById('serialFirmwarePath').value = res.path;
            document.getElementById('serialFlashBtn').disabled = false;
        }
    }

    async function flashFirmware() {
        if (isFlashing) return;
        if (!isConnected) return alert('Connect first');

        const path = document.getElementById('serialFirmwarePath').value;
        const baud = Number(document.getElementById('serialFlashBaud').value);
        const type = document.getElementById('serialFlashType').value;
        const offset = type === 'full' ? '0x0000' : '0x10000';

        // HIGH FIX: Stronger warning for full firmware flash
        if (type === 'full') {
            if (!confirm('⚠️ WARNING: Full Firmware mode writes to 0x0000.\n\n' +
                'This will ERASE the bootloader and partition table!\n' +
                'Only use this with a complete firmware.bin that includes bootloader.\n\n' +
                'If you flash just an app.bin at 0x0000, your device will be BRICKED!\n\n' +
                'Are you ABSOLUTELY SURE you want to continue?')) {
                return;
            }
        } else {
            if (!confirm('Start flashing? Device will restart.')) return;
        }

        isFlashing = true;
        const flashBtn = document.getElementById('serialFlashBtn');
        flashBtn.disabled = true;
        document.getElementById('serialFlashProgress').style.display = 'block';

        const logId = 'serialFirmwareLog';
        logToElement(logId, 'Starting flash...', 'response');

        // LOW FIX: Remove previous listeners to prevent memory leak
        window.electronAPI.removeFlashListeners?.();

        window.electronAPI.onFlashProgress((data) => {
            if (data.percent) {
                document.getElementById('serialProgressFill').style.width = data.percent + '%';
                document.getElementById('serialProgressText').textContent = data.percent + '%';
            }
        });

        window.electronAPI.onFlashDone((res) => {
            logToElement(logId, res.success ? 'Success!' : 'Failed: ' + res.message, res.success ? 'response' : 'error');
            isFlashing = false;
            flashBtn.disabled = false;
        });

        const res = await window.electronAPI.flashFirmware({
            portPath: currentPort,
            firmwarePath: path,
            baud,
            offset
        });

        if (!res.success) {
            logToElement(logId, 'Flash failed to start: ' + res.message, 'error');
            isFlashing = false;
            flashBtn.disabled = false;
        }
    }
}

// Helper to build commands from form - FIXED: Use firmware-compatible command names
function buildBasicConfigCommands(p) {
    const getVal = (id) => document.getElementById(p + id).value;

    const ssid = getVal('wifiSsid').trim();
    const pass = getVal('wifiPassword');
    const broker = getVal('mqttBroker').trim();
    const portRaw = getVal('mqttPort').trim();
    const mqttUser = getVal('mqttUsername').trim();
    const mqttPass = getVal('mqttPassword');
    const deviceId = getVal('deviceId').trim();

    if (!ssid) return { error: 'WiFi SSID is required' };
    if (!broker) return { error: 'MQTT broker is required' };
    const port = Number(portRaw);
    if (!Number.isInteger(port) || port <= 0 || port > 65535) return { error: 'MQTT port must be 1-65535' };
    if (!deviceId) return { error: 'Device ID is required' };

    const cmds = [];

    // WiFi: protect existing password (firmware does not print it)
    const loadedSsid = loadedState?.wifiSsid ?? '';
    if (pass) {
        cmds.push(`SET_WIFI:${ssid}:${pass}`);
    } else if (loadedState?.hasReadConfig && loadedSsid && ssid === loadedSsid) {
        // keep current WiFi password by skipping SET_WIFI
    } else if (loadedState?.hasReadConfig) {
        return { error: 'WiFi password is empty. Enter password to change WiFi settings.' };
    } else {
        return { error: 'WiFi password is empty. Enter password (or Load Basic first to keep current).' };
    }

    cmds.push(`SET_MQTT:${broker}:${port}`);

    // MQTT auth: protect existing password (firmware does not print it)
    const loadedUser = loadedState?.mqttUsername ?? '';
    if (mqttPass) {
        if (!mqttUser) return { error: 'MQTT Username is required when setting password' };
        cmds.push(`SET_MQTT_AUTH:${mqttUser}:${mqttPass}`);
    } else if (mqttUser) {
        if (loadedState?.hasReadConfig && loadedUser && mqttUser === loadedUser) {
            // keep existing auth password by skipping SET_MQTT_AUTH
        } else {
            return { error: 'MQTT password is empty. Enter password to set/change MQTT auth.' };
        }
    }

    cmds.push(`SET_DEVICE_ID:${deviceId}`);
    cmds.push('SAVE_CONFIG');
    return { cmds };
}

function buildExtraConfigCommands(p) {
    const getVal = (id) => document.getElementById(p + id).value;
    const getChk = (id) => document.getElementById(p + id).checked;

    const cmds = [];

    // Vending
    cmds.push(`SET_PRICE:${getVal('pricePerLiter')}`);
    cmds.push(`SET_TIMEOUT:${getVal('sessionTimeout')}`);
    cmds.push(`SET_RELAY_ACTIVE:${getChk('relayActiveHigh') ? 1 : 0}`);
    cmds.push(`SET_FREE_WATER:${getChk('enableFreeWater') ? 1 : 0}`);
    cmds.push(`SET_FREE_WATER_COOLDOWN:${getVal('freeWaterCooldown')}`);
    cmds.push(`SET_FREE_WATER_AMOUNT:${getVal('freeWaterAmount')}`);

    // Sensors
    cmds.push(`SET_PULSES_PER_LITER:${getVal('pulsesPerLiter')}`);
    cmds.push(`SET_TDS_THRESHOLD:${getVal('tdsThreshold')}`);
    cmds.push(`SET_TDS_TEMP:${getVal('tdsTemperatureC')}`);
    cmds.push(`SET_TDS_CALIB:${getVal('tdsCalibrationFactor')}`);

    // Cash
    cmds.push(`SET_CASH_PULSE:${getVal('cashPulseValue')}`);
    cmds.push(`SET_CASH_GAP:${getVal('cashPulseGapMs')}`);

    // Intervals
    cmds.push(`SET_PAYMENT_INTERVAL:${getVal('paymentCheckInterval')}`);
    cmds.push(`SET_DISPLAY_INTERVAL:${getVal('displayUpdateInterval')}`);
    cmds.push(`SET_TDS_INTERVAL:${getVal('tdsCheckInterval')}`);
    cmds.push(`SET_HEARTBEAT_INTERVAL:${getVal('heartbeatInterval')}`);

    // Group ID
    if (getVal('groupId')) {
        cmds.push(`SET_GROUP:${getVal('groupId')}`);
    }

    // Security settings
    if (getVal('apiSecret')) {
        cmds.push(`SET_API_SECRET:${getVal('apiSecret')}`);
    }
    cmds.push(`SET_REQUIRE_SIGNED:${getChk('requireSigned') ? 1 : 0}`);
    cmds.push(`SET_ALLOW_REMOTE_NETCFG:${getChk('allowRemoteNetworkConfig') ? 1 : 0}`);

    cmds.push('SAVE_CONFIG');
    return cmds;
}

function parseConfigLine(line, p, loadedState) {
    // Parser implementation for `printCurrentConfig()` output
    const setVal = (id, v) => {
        const el = document.getElementById(p + id);
        if (el) el.value = v;
    };
    const setChk = (id, v) => {
        const el = document.getElementById(p + id);
        if (el) el.checked = !!v;
    };

    const textAfter = (label) => line.split(label)[1]?.trim() ?? '';

    // WiFi
    if (line.includes('SSID:')) {
        const val = textAfter('SSID:');
        if (loadedState) {
            loadedState.hasReadConfig = true;
            loadedState.wifiSsid = val && val !== '(not set)' ? val : '';
        }
        setVal('wifiSsid', val && val !== '(not set)' ? val : '');
        setVal('wifiPassword', ''); // password is masked in firmware output
        setVal('mqttPassword', ''); // keep empty unless user types it
        return;
    }
    if (line.includes('Password:')) {
        if (loadedState) loadedState.hasReadConfig = true;
        setVal('wifiPassword', '');
        return;
    }

    // MQTT
    if (line.includes('Broker:')) {
        const val = textAfter('Broker:');
        if (loadedState) loadedState.hasReadConfig = true;
        if (val) setVal('mqttBroker', val);
        return;
    }
    if (line.includes('Port:')) {
        const val = parseInt(textAfter('Port:'), 10);
        if (loadedState) loadedState.hasReadConfig = true;
        if (!isNaN(val)) setVal('mqttPort', val);
        return;
    }
    if (line.includes('Device ID:')) {
        const val = textAfter('Device ID:');
        if (loadedState) loadedState.hasReadConfig = true;
        if (val) setVal('deviceId', val);
        return;
    }
    if (line.includes('Group ID:')) {
        const val = textAfter('Group ID:');
        if (loadedState) loadedState.hasReadConfig = true;
        setVal('groupId', val && val !== '(not set)' ? val : '');
        return;
    }
    if (line.includes('Username:')) {
        const val = textAfter('Username:');
        if (loadedState) {
            loadedState.hasReadConfig = true;
            loadedState.mqttUsername = val && val !== '(not set)' ? val : '';
        }
        setVal('mqttUsername', val && val !== '(not set)' ? val : '');
        setVal('mqttPassword', ''); // password is masked/not printed
        return;
    }
    if (line.includes('Require Signed:')) {
        if (loadedState) loadedState.hasReadConfig = true;
        setChk('requireSigned', line.includes('YES'));
        return;
    }
    if (line.includes('Remote Network Config:')) {
        if (loadedState) loadedState.hasReadConfig = true;
        setChk('allowRemoteNetworkConfig', line.includes('Allowed'));
        return;
    }

    // Vending / Sensors
    if (line.includes('Price per Liter:')) {
        const val = parseInt(textAfter('Price per Liter:'), 10);
        if (!isNaN(val)) setVal('pricePerLiter', val);
        return;
    }
    if (line.includes('Session Timeout:')) {
        const val = parseInt(textAfter('Session Timeout:'), 10);
        if (!isNaN(val)) setVal('sessionTimeout', val);
        return;
    }
    if (line.includes('Free Water Cooldown:')) {
        const val = parseInt(textAfter('Free Water Cooldown:'), 10);
        if (!isNaN(val)) setVal('freeWaterCooldown', val);
        return;
    }
    if (line.includes('Free Water Amount:')) {
        const val = parseInt(textAfter('Free Water Amount:'), 10);
        if (!isNaN(val)) setVal('freeWaterAmount', val);
        return;
    }
    if (line.includes('Pulses per Liter:')) {
        const val = parseFloat(textAfter('Pulses per Liter:'));
        if (!isNaN(val)) setVal('pulsesPerLiter', val);
        return;
    }
    if (line.includes('TDS Threshold:')) {
        const val = parseInt(textAfter('TDS Threshold:'), 10);
        if (!isNaN(val)) setVal('tdsThreshold', val);
        return;
    }
    if (line.includes('TDS Temperature:')) {
        const val = parseFloat(textAfter('TDS Temperature:'));
        if (!isNaN(val)) setVal('tdsTemperatureC', val);
        return;
    }
    if (line.includes('TDS Calibration:')) {
        const val = parseFloat(textAfter('TDS Calibration:'));
        if (!isNaN(val)) setVal('tdsCalibrationFactor', val);
        return;
    }
    if (line.includes('Free Water:')) {
        setChk('enableFreeWater', line.includes('Enabled'));
        return;
    }
    if (line.includes('Relay Active High:')) {
        setChk('relayActiveHigh', line.includes('YES'));
        return;
    }
    if (line.includes('Cash Pulse Value:')) {
        const val = parseInt(textAfter('Cash Pulse Value:'), 10);
        if (!isNaN(val)) setVal('cashPulseValue', val);
        return;
    }
    if (line.includes('Cash Pulse Gap:')) {
        const val = parseInt(textAfter('Cash Pulse Gap:'), 10);
        if (!isNaN(val)) setVal('cashPulseGapMs', val);
        return;
    }

    // Intervals
    if (line.includes('Payment Interval:')) {
        const val = parseInt(textAfter('Payment Interval:'), 10);
        if (!isNaN(val)) setVal('paymentCheckInterval', val);
        return;
    }
    if (line.includes('Display Interval:')) {
        const val = parseInt(textAfter('Display Interval:'), 10);
        if (!isNaN(val)) setVal('displayUpdateInterval', val);
        return;
    }
    if (line.includes('TDS Interval:')) {
        const val = parseInt(textAfter('TDS Interval:'), 10);
        if (!isNaN(val)) setVal('tdsCheckInterval', val);
        return;
    }
    if (line.includes('Heartbeat Interval:')) {
        const val = parseInt(textAfter('Heartbeat Interval:'), 10);
        if (!isNaN(val)) setVal('heartbeatInterval', val);
        return;
    }

    // Power Management
    if (line.includes('Enable Power Save:')) {
        setChk('enablePowerSave', line.includes('YES'));
        return;
    }
    if (line.includes('Deep Sleep Window:')) {
        // "  Deep Sleep Window: 1:00 - 6:00"
        try {
            const windowText = textAfter('Deep Sleep Window:');
            const parts = windowText.split('-');
            const start = parseInt(parts[0].trim().split(':')[0], 10);
            const end = parseInt(parts[1].trim().split(':')[0], 10);
            if (!isNaN(start)) setVal('deepSleepStartHour', start);
            if (!isNaN(end)) setVal('deepSleepEndHour', end);
        } catch (e) {
            console.error('Failed to parse deep sleep window', e);
        }
    }
}

// ============================================
// eWater Device Manager - Renderer Process
// ============================================

let currentDevice = null;
let isConnected = false;
const MAX_LOG_LINES = 500;
let loadedWifiSsid = '';
let wifiPasswordMasked = false;
let loadedMqttUsername = '';
let apiSecretMasked = false;

// DOM Elements
const elements = {
    // Devices
    deviceList: document.getElementById('deviceList'),
    scanBtn: document.getElementById('scanBtn'),
    connectionStatus: document.getElementById('connectionStatus'),

    // Tabs
    tabs: document.querySelectorAll('.tab'),
    tabContents: document.querySelectorAll('.tab-content'),

    // Config Form
    wifiSsid: document.getElementById('wifiSsid'),
    wifiPassword: document.getElementById('wifiPassword'),
    mqttBroker: document.getElementById('mqttBroker'),
    mqttPort: document.getElementById('mqttPort'),
    mqttUsername: document.getElementById('mqttUsername'),
    mqttPassword: document.getElementById('mqttPassword'),
    deviceId: document.getElementById('deviceId'),
    pricePerLiter: document.getElementById('pricePerLiter'),
    sessionTimeout: document.getElementById('sessionTimeout'),
    enableFreeWater: document.getElementById('enableFreeWater'),
    freeWaterCooldown: document.getElementById('freeWaterCooldown'),
    freeWaterAmount: document.getElementById('freeWaterAmount'),
    pulsesPerLiter: document.getElementById('pulsesPerLiter'),
    tdsThreshold: document.getElementById('tdsThreshold'),
    tdsTemperatureC: document.getElementById('tdsTemperatureC'),
    tdsCalibrationFactor: document.getElementById('tdsCalibrationFactor'),
    cashPulseValue: document.getElementById('cashPulseValue'),
    cashPulseGapMs: document.getElementById('cashPulseGapMs'),
    paymentCheckInterval: document.getElementById('paymentCheckInterval'),
    displayUpdateInterval: document.getElementById('displayUpdateInterval'),
    tdsCheckInterval: document.getElementById('tdsCheckInterval'),
    heartbeatInterval: document.getElementById('heartbeatInterval'),
    applyMode: document.getElementById('applyMode'),
    apiSecret: document.getElementById('apiSecret'),
    requireSigned: document.getElementById('requireSigned'),
    allowRemoteNetcfg: document.getElementById('allowRemoteNetcfg'),

    // Actions
    loadConfigBtn: document.getElementById('loadConfigBtn'),
    saveConfigBtn: document.getElementById('saveConfigBtn'),

    // Firmware
    firmwarePath: document.getElementById('firmwarePath'),
    browseFirmwareBtn: document.getElementById('browseFirmwareBtn'),
    flashFirmwareBtn: document.getElementById('flashFirmwareBtn'),
    flashProgress: document.getElementById('flashProgress'),
    progressFill: document.getElementById('progressFill'),
    progressText: document.getElementById('progressText'),
    flashBaud: document.getElementById('flashBaud'),
    flashType: document.getElementById('flashType'),
    firmwareLog: document.getElementById('firmwareLog'),

    // Serial Monitor
    monitorOutput: document.getElementById('monitorOutput'),
    commandInput: document.getElementById('commandInput'),
    sendCommandBtn: document.getElementById('sendCommandBtn')
};

// ============================================
// Tab Switching
// ============================================
elements.tabs.forEach(tab => {
    tab.addEventListener('click', () => {
        const tabName = tab.dataset.tab;

        // Update tabs
        elements.tabs.forEach(t => t.classList.remove('active'));
        tab.classList.add('active');

        // Update tab content
        elements.tabContents.forEach(content => {
            content.classList.remove('active');
        });
        document.getElementById(tabName + 'Tab').classList.add('active');
    });
});

// ============================================
// Device Scanning
// ============================================
elements.scanBtn.addEventListener('click', async () => {
    elements.scanBtn.disabled = true;
    elements.scanBtn.textContent = '🔄 Scanning...';

    try {
        const ports = await window.electronAPI.scanPorts();
        displayDevices(ports);
    } catch (error) {
        logToMonitor('Error scanning ports: ' + error.message, 'error');
    } finally {
        elements.scanBtn.disabled = false;
        elements.scanBtn.textContent = '🔍 Scan Ports';
    }
});

function displayDevices(ports) {
    if (ports.length === 0) {
        elements.deviceList.innerHTML = '<p class="placeholder">No devices found</p>';
        return;
    }

    elements.deviceList.innerHTML = '';
    ports.forEach(port => {
        const deviceItem = document.createElement('div');
        deviceItem.className = 'device-item';
        deviceItem.innerHTML = `
            <div class="device-path">${port.path}</div>
            <div class="device-info">${port.manufacturer || 'Unknown'}</div>
        `;

        deviceItem.addEventListener('click', () => connectToDevice(port.path, deviceItem));
        elements.deviceList.appendChild(deviceItem);
    });
}

// ============================================
// Device Connection
// ============================================
async function connectToDevice(portPath, deviceElement) {
    if (isConnected && currentDevice === portPath) {
        // Disconnect
        await window.electronAPI.disconnectDevice();
        updateConnectionStatus(false);
        deviceElement.classList.remove('active');
        return;
    }

    try {
        logToMonitor(`Connecting to ${portPath}...`, 'command');
        const result = await window.electronAPI.connectDevice(portPath);

        if (result.success) {
            // Remove active from all devices
            document.querySelectorAll('.device-item').forEach(d => d.classList.remove('active'));
            deviceElement.classList.add('active');

            currentDevice = portPath;
            updateConnectionStatus(true);
            logToMonitor('Connected successfully!', 'response');

            // Auto-load config
            setTimeout(() => loadConfigFromDevice(), 500);
        } else {
            logToMonitor('Connection failed: ' + result.message, 'error');
        }
    } catch (error) {
        logToMonitor('Connection error: ' + error.message, 'error');
    }
}

function updateConnectionStatus(connected) {
    isConnected = connected;
    const statusDot = elements.connectionStatus.querySelector('.status-dot');
    const statusText = elements.connectionStatus.querySelector('span:last-child');

    if (connected) {
        statusDot.classList.add('connected');
        statusText.textContent = 'Connected';
    } else {
        statusDot.classList.remove('connected');
        statusText.textContent = 'Disconnected';
        currentDevice = null;
    }
}

// ============================================
// Configuration Management
// ============================================
elements.loadConfigBtn.addEventListener('click', loadConfigFromDevice);
elements.saveConfigBtn.addEventListener('click', saveConfigToDevice);

async function loadConfigFromDevice() {
    if (!isConnected) {
        alert('Please connect to a device first');
        return;
    }

    logToMonitor('> GET_CONFIG', 'command');
    await sendCommand('GET_CONFIG');
}

async function saveConfigToDevice() {
    if (!isConnected) {
        alert('Please connect to a device first');
        return;
    }

    const validationError = validateConfigInputs();
    if (validationError) {
        alert(validationError);
        return;
    }

    // Build configuration commands
    const commands = [];
    const ssid = elements.wifiSsid.value.trim();
    const wifiPass = elements.wifiPassword.value;
    const mqttUser = elements.mqttUsername.value.trim();
    const mqttPass = elements.mqttPassword.value;
    const apiSecret = elements.apiSecret.value;

    if (!(wifiPasswordMasked && !wifiPass && ssid === loadedWifiSsid)) {
        commands.push(`SET_WIFI:${ssid}:${wifiPass}`);
    }
    commands.push(`SET_MQTT:${elements.mqttBroker.value}:${elements.mqttPort.value}`);

    if (mqttUser || mqttPass) {
        commands.push(`SET_MQTT_AUTH:${mqttUser}:${mqttPass}`);
    }

    if (!apiSecretMasked || apiSecret) {
        if (apiSecret.length > 0) {
            commands.push(`SET_API_SECRET:${apiSecret}`);
        }
    }

    commands.push(
        `SET_DEVICE_ID:${elements.deviceId.value}`,
        `SET_PRICE:${elements.pricePerLiter.value}`,
        `SET_TIMEOUT:${elements.sessionTimeout.value}`,
        `SET_FREE_WATER:${elements.enableFreeWater.checked ? 1 : 0}`,
        `SET_FREE_WATER_COOLDOWN:${elements.freeWaterCooldown.value}`,
        `SET_FREE_WATER_AMOUNT:${elements.freeWaterAmount.value}`,
        `SET_PULSES_PER_LITER:${elements.pulsesPerLiter.value}`,
        `SET_TDS_THRESHOLD:${elements.tdsThreshold.value}`,
        `SET_TDS_TEMP:${elements.tdsTemperatureC.value}`,
        `SET_TDS_CALIB:${elements.tdsCalibrationFactor.value}`,
        `SET_CASH_PULSE:${elements.cashPulseValue.value}`,
        `SET_CASH_GAP:${elements.cashPulseGapMs.value}`,
        `SET_PAYMENT_INTERVAL:${elements.paymentCheckInterval.value}`,
        `SET_DISPLAY_INTERVAL:${elements.displayUpdateInterval.value}`,
        `SET_TDS_INTERVAL:${elements.tdsCheckInterval.value}`,
        `SET_HEARTBEAT_INTERVAL:${elements.heartbeatInterval.value}`,
        `SET_REQUIRE_SIGNED:${elements.requireSigned.checked ? 1 : 0}`,
        `SET_ALLOW_REMOTE_NETCFG:${elements.allowRemoteNetcfg.checked ? 1 : 0}`,
        'SAVE_CONFIG'
    );

    // Send commands sequentially
    for (const cmd of commands) {
        logToMonitor('> ' + maskCommandForLog(cmd), 'command');
        await sendCommand(cmd);
        await delay(200); // Wait between commands
    }

    const applyMode = elements.applyMode.value;
    if (applyMode === 'now') {
        logToMonitor('> APPLY_CONFIG', 'command');
        await sendCommand('APPLY_CONFIG');
    } else if (applyMode === 'restart') {
        logToMonitor('> RESTART', 'command');
        await sendCommand('RESTART');
    }

    logToMonitor('Configuration saved!', 'response');
}

// ============================================
// Firmware Management
// ============================================
let isFlashing = false;

elements.browseFirmwareBtn.addEventListener('click', async () => {
    const result = await window.electronAPI.selectFirmware();
    if (result.success) {
        elements.firmwarePath.value = result.path;
        elements.flashFirmwareBtn.disabled = false;
    }
});

elements.flashFirmwareBtn.addEventListener('click', async () => {
    if (isFlashing) return;
    if (!isConnected) {
        alert('Please connect to a device first');
        return;
    }
    if (!elements.firmwarePath.value) {
        alert('Please select a firmware file');
        return;
    }

    if (!confirm('Flash firmware? Device will restart.')) {
        return;
    }

    isFlashing = true;
    setFlashUiState(true);
    logFirmware('Starting flash...');

    const baud = Number(elements.flashBaud.value) || 460800;
    const flashType = elements.flashType.value || 'app';
    const offset = flashType === 'full' ? '0x0000' : '0x10000';
    if (flashType === 'full') {
        const name = elements.firmwarePath.value.toLowerCase();
        if (!name.includes('full')) {
            const proceed = confirm('This file does not look like a full firmware image. Continue anyway?');
            if (!proceed) {
                setFlashUiState(false);
                isFlashing = false;
                return;
            }
        }
    }
    logFirmware(`Flash type: ${flashType === 'full' ? 'Full' : 'App'} (${offset})`);
    const result = await window.electronAPI.flashFirmware({
        portPath: currentDevice,
        firmwarePath: elements.firmwarePath.value,
        baud,
        offset
    });

    if (!result.success) {
        logFirmware('Flash error: ' + result.message);
        setFlashUiState(false);
        isFlashing = false;
        return;
    }
});

window.electronAPI.onFlashProgress((data) => {
    if (typeof data?.percent === 'number') {
        elements.flashProgress.style.display = 'block';
        elements.progressFill.style.width = `${data.percent}%`;
        elements.progressText.textContent = `${data.percent}%`;
    }
    if (data?.message) {
        logFirmware(data.message);
    }
});

window.electronAPI.onFlashDone((data) => {
    if (data?.success) {
        logFirmware('Flash completed successfully.');
    } else {
        logFirmware('Flash failed: ' + (data?.message || 'Unknown error'));
    }
    setFlashUiState(false);
    isFlashing = false;
    updateConnectionStatus(false);
});

function setFlashUiState(active) {
    elements.flashFirmwareBtn.disabled = active;
    elements.browseFirmwareBtn.disabled = active;
    if (!active) {
        elements.progressFill.style.width = '0%';
        elements.progressText.textContent = '0%';
        elements.flashProgress.style.display = 'none';
    }
}

function logFirmware(message) {
    const line = document.createElement('div');
    line.textContent = message;
    elements.firmwareLog.appendChild(line);
    elements.firmwareLog.scrollTop = elements.firmwareLog.scrollHeight;
    const lines = elements.firmwareLog.children;
    if (lines.length > 200) {
        elements.firmwareLog.removeChild(lines[0]);
    }
}

// ============================================
// Serial Communication
// ============================================
elements.sendCommandBtn.addEventListener('click', sendInputCommand);
elements.commandInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        sendInputCommand();
    }
});

async function sendInputCommand() {
    const command = elements.commandInput.value.trim();
    if (!command) return;

    if (!isConnected) {
        alert('Please connect to a device first');
        return;
    }

    logToMonitor('> ' + command, 'command');
    await sendCommand(command);
    elements.commandInput.value = '';
}

async function sendCommand(command) {
    try {
        const result = await window.electronAPI.sendCommand(command);
        if (!result.success) {
            logToMonitor('Send error: ' + result.message, 'error');
        }
    } catch (error) {
        logToMonitor('Send error: ' + error.message, 'error');
    }
}

// Listen for serial data
window.electronAPI.onSerialData((data) => {
    logToMonitor('< ' + data, 'response');

    // Parse config response
    parseConfigLine(data);
});

// Listen for serial errors
window.electronAPI.onSerialError((error) => {
    logToMonitor('Serial error: ' + error, 'error');
    updateConnectionStatus(false);
});

window.electronAPI.onSerialClosed((message) => {
    logToMonitor(message, 'error');
    updateConnectionStatus(false);
});

// ============================================
// Serial Monitor Logging
// ============================================
function logToMonitor(message, type = 'response') {
    const logLine = document.createElement('div');
    logLine.className = `log-line ${type}`;
    logLine.textContent = message;
    elements.monitorOutput.appendChild(logLine);
    elements.monitorOutput.scrollTop = elements.monitorOutput.scrollHeight;

    // Keep log size bounded to avoid memory bloat
    const lines = elements.monitorOutput.children;
    if (lines.length > MAX_LOG_LINES) {
        elements.monitorOutput.removeChild(lines[0]);
    }
}

// ============================================
// Config Parsing
// ============================================
function parseConfigLine(line) {
    // Parse config values from device
    if (line.includes('SSID:')) {
        const value = line.split('SSID:')[1].trim();
        if (value && value !== '(not set)') {
            elements.wifiSsid.value = value;
            loadedWifiSsid = value;
        } else {
            elements.wifiSsid.value = '';
            loadedWifiSsid = '';
        }
    } else if (line.includes('Password:')) {
        const value = line.split('Password:')[1].trim();
        if (value === '(not set)') {
            elements.wifiPassword.value = '';
            wifiPasswordMasked = false;
        } else if (value === '********') {
            elements.wifiPassword.value = '';
            wifiPasswordMasked = true;
        }
    } else if (line.includes('Broker:')) {
        const value = line.split('Broker:')[1].trim();
        elements.mqttBroker.value = value;
    } else if (line.includes('Port:')) {
        const value = line.split('Port:')[1].trim();
        elements.mqttPort.value = value;
    } else if (line.includes('Device ID:')) {
        const value = line.split('Device ID:')[1].trim();
        elements.deviceId.value = value;
    } else if (line.includes('Username:')) {
        const value = line.split('Username:')[1].trim();
        if (value && value !== '(not set)') {
            elements.mqttUsername.value = value;
            loadedMqttUsername = value;
        } else {
            elements.mqttUsername.value = '';
            loadedMqttUsername = '';
        }
    } else if (line.includes('API Secret:')) {
        const value = line.split('API Secret:')[1].trim();
        if (value === '********') {
            apiSecretMasked = true;
            elements.apiSecret.value = '';
        } else {
            apiSecretMasked = false;
            elements.apiSecret.value = '';
        }
    } else if (line.includes('Require Signed:')) {
        elements.requireSigned.checked = line.includes('YES');
    } else if (line.includes('Remote Network Config:')) {
        elements.allowRemoteNetcfg.checked = line.includes('Allowed');
    } else if (line.includes('Price per Liter:')) {
        const value = line.split('Price per Liter:')[1].replace('so\'m', '').trim();
        elements.pricePerLiter.value = value;
    } else if (line.includes('Session Timeout:')) {
        const value = line.split('Session Timeout:')[1].replace('sec', '').trim();
        elements.sessionTimeout.value = value;
    } else if (line.includes('Free Water Cooldown:')) {
        const value = line.split('Free Water Cooldown:')[1].replace('sec', '').trim();
        elements.freeWaterCooldown.value = value;
    } else if (line.includes('Free Water Amount:')) {
        const value = line.split('Free Water Amount:')[1].replace('ml', '').trim();
        elements.freeWaterAmount.value = value;
    } else if (line.includes('Pulses per Liter:')) {
        const value = line.split('Pulses per Liter:')[1].trim();
        elements.pulsesPerLiter.value = value;
    } else if (line.includes('TDS Threshold:')) {
        const value = line.split('TDS Threshold:')[1].replace('ppm', '').trim();
        elements.tdsThreshold.value = value;
    } else if (line.includes('TDS Temperature:')) {
        const value = line.split('TDS Temperature:')[1].replace('C', '').trim();
        elements.tdsTemperatureC.value = value;
    } else if (line.includes('TDS Calibration:')) {
        const value = line.split('TDS Calibration:')[1].trim();
        elements.tdsCalibrationFactor.value = value;
    } else if (line.includes('Free Water:')) {
        elements.enableFreeWater.checked = line.includes('Enabled');
    } else if (line.includes('Cash Pulse Value:')) {
        const value = line.split('Cash Pulse Value:')[1].replace('so\'m', '').trim();
        elements.cashPulseValue.value = value;
    } else if (line.includes('Cash Pulse Gap:')) {
        const value = line.split('Cash Pulse Gap:')[1].replace('ms', '').trim();
        elements.cashPulseGapMs.value = value;
    } else if (line.includes('Payment Interval:')) {
        const value = line.split('Payment Interval:')[1].replace('ms', '').trim();
        elements.paymentCheckInterval.value = value;
    } else if (line.includes('Display Interval:')) {
        const value = line.split('Display Interval:')[1].replace('ms', '').trim();
        elements.displayUpdateInterval.value = value;
    } else if (line.includes('TDS Interval:')) {
        const value = line.split('TDS Interval:')[1].replace('ms', '').trim();
        elements.tdsCheckInterval.value = value;
    } else if (line.includes('Heartbeat Interval:')) {
        const value = line.split('Heartbeat Interval:')[1].replace('ms', '').trim();
        elements.heartbeatInterval.value = value;
    }
}

// ============================================
// Utilities
// ============================================
function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function validateConfigInputs() {
    const ssid = elements.wifiSsid.value.trim();
    const password = elements.wifiPassword.value;
    const broker = elements.mqttBroker.value.trim();
    const port = Number(elements.mqttPort.value);
    const mqttUser = elements.mqttUsername.value.trim();
    const mqttPass = elements.mqttPassword.value;
    const deviceId = elements.deviceId.value.trim();
    const price = Number(elements.pricePerLiter.value);
    const timeoutSec = Number(elements.sessionTimeout.value);
    const cooldownSec = Number(elements.freeWaterCooldown.value);
    const freeAmount = Number(elements.freeWaterAmount.value);
    const pulses = Number(elements.pulsesPerLiter.value);
    const tdsThreshold = Number(elements.tdsThreshold.value);
    const cashPulseValue = Number(elements.cashPulseValue.value);
    const cashGap = Number(elements.cashPulseGapMs.value);
    const paymentInterval = Number(elements.paymentCheckInterval.value);
    const displayInterval = Number(elements.displayUpdateInterval.value);
    const tdsInterval = Number(elements.tdsCheckInterval.value);
    const heartbeatInterval = Number(elements.heartbeatInterval.value);

    if (!ssid) return 'WiFi SSID is required';
    if (!broker) return 'MQTT broker is required';
    if (!deviceId) return 'Device ID is required';
    if (!Number.isInteger(port) || port <= 0 || port > 65535) return 'MQTT port must be 1-65535';

    if (ssid.length >= 32) return 'WiFi SSID too long (max 31 chars)';
    if (password.length >= 64) return 'WiFi password too long (max 63 chars)';
    if (broker.length >= 128) return 'MQTT broker too long (max 127 chars)';
    if (mqttUser.length >= 32) return 'MQTT username too long (max 31 chars)';
    if (mqttPass.length >= 64) return 'MQTT password too long (max 63 chars)';
    if (deviceId.length >= 32) return 'Device ID too long (max 31 chars)';
    const apiSecretValue = elements.apiSecret.value;
    if (apiSecretValue.length >= 64) return 'API secret too long (max 63 chars)';

    if (wifiPasswordMasked && ssid !== loadedWifiSsid && !password) {
        return 'WiFi password is required when SSID changes';
    }

    const hasColon = [ssid, password, broker, deviceId, mqttUser, mqttPass, apiSecretValue]
        .some(v => v.includes(':'));
    if (hasColon) return 'Fields cannot contain ":" character';

    if (!Number.isFinite(price) || price < 1 || price > 100000) {
        return 'Price per liter must be 1-100000';
    }
    if (!Number.isFinite(timeoutSec) || timeoutSec < 60 || timeoutSec > 3600) {
        return 'Session timeout must be 60-3600 sec';
    }
    if (!Number.isFinite(cooldownSec) || cooldownSec < 60 || cooldownSec > 7200) {
        return 'Free water cooldown must be 60-7200 sec';
    }
    if (!Number.isFinite(freeAmount) || freeAmount < 1 || freeAmount > 5000) {
        return 'Free water amount must be 1-5000 ml';
    }
    if (!Number.isFinite(pulses) || pulses < 1 || pulses > 5000) {
        return 'Pulses per liter must be 1-5000';
    }
    if (!Number.isFinite(tdsThreshold) || tdsThreshold < 0 || tdsThreshold > 5000) {
        return 'TDS threshold must be 0-5000';
    }
    const tdsTemp = Number(elements.tdsTemperatureC.value);
    const tdsCalib = Number(elements.tdsCalibrationFactor.value);
    if (!Number.isFinite(tdsTemp) || tdsTemp < 0 || tdsTemp > 80) {
        return 'TDS temperature must be 0-80 C';
    }
    if (!Number.isFinite(tdsCalib) || tdsCalib <= 0 || tdsCalib > 5) {
        return 'TDS calibration must be 0-5';
    }
    if (!Number.isFinite(cashPulseValue) || cashPulseValue < 1 || cashPulseValue > 100000) {
        return 'Cash pulse value must be 1-100000';
    }
    if (!Number.isFinite(cashGap) || cashGap < 20 || cashGap > 1000) {
        return 'Cash pulse gap must be 20-1000 ms';
    }
    if (!Number.isFinite(paymentInterval) || paymentInterval < 200 || paymentInterval > 600000) {
        return 'Payment interval must be 200-600000 ms';
    }
    if (!Number.isFinite(displayInterval) || displayInterval < 50 || displayInterval > 10000) {
        return 'Display interval must be 50-10000 ms';
    }
    if (!Number.isFinite(tdsInterval) || tdsInterval < 1000 || tdsInterval > 600000) {
        return 'TDS interval must be 1000-600000 ms';
    }
    if (!Number.isFinite(heartbeatInterval) || heartbeatInterval < 1000 || heartbeatInterval > 3600000) {
        return 'Heartbeat interval must be 1000-3600000 ms';
    }

    if (elements.requireSigned.checked && !apiSecret && !apiSecretMasked) {
        return 'API secret is required when signed messages are enabled';
    }
    return null;
}

function maskCommandForLog(cmd) {
    if (cmd.startsWith('SET_WIFI:')) {
        const parts = cmd.split(':');
        if (parts.length >= 3) {
            return `SET_WIFI:${parts[1]}:***`;
        }
    }
    if (cmd.startsWith('SET_MQTT_AUTH:')) {
        const parts = cmd.split(':');
        if (parts.length >= 3) {
            return `SET_MQTT_AUTH:${parts[1]}:***`;
        }
    }
    if (cmd.startsWith('SET_API_SECRET:')) {
        return 'SET_API_SECRET:***';
    }
    return cmd;
}

// ============================================
// Initialize
// ============================================
logToMonitor('eWater Device Manager ready. Click "Scan Ports" to find devices.', 'response');

// Auto-scan on startup
setTimeout(() => {
    elements.scanBtn.click();
}, 500);

import { logToElement } from './utils.js';

export function setupOnline(prefix) {
    const p = prefix; // 'online_'

    // State
    let isConnected = false;
    let selectedDevice = null;
    let devices = new Map(); // id -> { lastSeen, firmware, ... }

    // Elements
    const connectBtn = document.getElementById('mqttConnectBtn');
    const mqttStatusDot = document.getElementById('mqttStatusDot');
    const mqttStatusText = document.getElementById('mqttStatusText');
    const devicesList = document.getElementById('onlineDevicesList');

    // Config
    const applyBasicConfigBtn = document.getElementById('onlineApplyBasicConfigBtn');
    const applyExtraConfigBtn = document.getElementById('onlineApplyExtraConfigBtn');
    const selectedDeviceConfigEls = document.querySelectorAll('.selectedDeviceConfigText');

    // Monitor
    const monitorOutput = 'onlineMonitorOutput';
    const clearMonitorBtn = document.getElementById('clearOnlineMonitorBtn');
    const selectedDeviceMonitor = document.getElementById('selectedDeviceMonitor');

    // Event Listeners
    connectBtn.addEventListener('click', toggleConnection);
    applyBasicConfigBtn?.addEventListener('click', () => sendConfig('basic'));
    applyExtraConfigBtn?.addEventListener('click', () => sendConfig('extra'));

    clearMonitorBtn.addEventListener('click', () => {
        document.getElementById(monitorOutput).innerHTML = '';
    });

    // MQTT Listeners
    window.electronAPI.onMqttStatus((data) => {
        updateStatus(data.status, data.message);
    });

    window.electronAPI.onMqttMessage((data) => {
        handleMqttMessage(data.topic, data.message);
    });

    // Online mode: some config fields are serial-only on current firmware.
    const disableConfigField = (id, reason) => {
        const el = document.getElementById(p + id);
        if (!el) return;
        el.disabled = true;
        el.title = reason || 'This setting is not supported by the current firmware';
    };
    disableConfigField('requireSigned', 'RequireSigned is set via Serial config');
    disableConfigField('allowRemoteNetworkConfig', 'AllowRemoteNetworkConfig is set via Serial config');
    disableConfigField('groupId', 'GroupId is set via Serial config');
    disableConfigField('enableFreeWater', 'Free-water settings are not supported by the current firmware');
    disableConfigField('freeWaterCooldown', 'Free-water settings are not supported by the current firmware');
    disableConfigField('freeWaterAmount', 'Free-water settings are not supported by the current firmware');

    const apiSecretEl = document.getElementById(p + 'apiSecret');
    if (apiSecretEl) {
        apiSecretEl.placeholder = 'Enter current API secret to sign messages';
    }

    // FUNCTIONS

    function makeNonce(prefix) {
        return `${prefix}_${Date.now()}_${Math.random().toString(16).slice(2, 10)}`;
    }

    async function toggleConnection() {
        if (isConnected) {
            // Disconnect not strictly implemented in main API provided,
            // but we can just reconnect with invalid creds or add disconnect handler to main.
            // For now, re-clicking connects again which disconnects previous.
            connectBtn.disabled = true;
            connectBtn.textContent = 'Reconnecting...';
        }

        const host = document.getElementById('mqttBrokerHost').value;
        const port = Number(document.getElementById('mqttBrokerPort').value);
        const user = document.getElementById('mqttBrokerUser').value;
        const pass = document.getElementById('mqttBrokerPass').value;

        updateStatus('connecting');

        await window.electronAPI.mqttConnect({
            host, port, username: user, password: pass
        });

        // Timeout handling done in main
        connectBtn.disabled = false;
    }

    function updateStatus(status, message) {
        let color = '#ea6045'; // red
        let text = 'Disconnected';

        if (status === 'connected') {
            color = '#4ec9b0'; // green
            text = 'Connected';
            isConnected = true;
            connectBtn.textContent = 'Reconnect';

            // Subscribe to heartbeats
            window.electronAPI.mqttSubscribe('vending/+/heartbeat');
        } else if (status === 'connecting' || status === 'reconnecting') {
            color = '#dcdcaa'; // yellow
            text = status;
        } else {
            isConnected = false;
            text = message ? `Error: ${message}` : 'Disconnected';
            connectBtn.textContent = 'Connect';
        }

        mqttStatusDot.style.backgroundColor = color;
        mqttStatusText.textContent = text;
        mqttStatusText.title = message || '';
    }

    function handleMqttMessage(topic, msgStr) {
        if (topic.endsWith('/heartbeat')) {
            handleHeartbeat(topic, msgStr);
        } else if (topic.endsWith('/status/out')) {
            if (!selectedDevice || !topic.includes(selectedDevice)) return;
            try {
                const data = JSON.parse(msgStr);
                const state = data.state || 'UNKNOWN';
                const bal = data.balance ?? '?';
                const tds = data.tds ?? '?';
                const last = data.last_dispense ?? '?';
                logToElement(monitorOutput, `[STATUS] ${state} | balance=${bal} | last=${last}L | tds=${tds}`, 'response');
            } catch {
                logToElement(monitorOutput, `[STATUS] ${msgStr}`, 'response');
            }
        } else if (topic.endsWith('/log/out')) {
            if (selectedDevice && topic.includes(selectedDevice)) {
                let display = msgStr;
                let event = '';
                let message = '';
                try {
                    const obj = JSON.parse(msgStr);
                    if (obj && typeof obj === 'object') {
                        event = String(obj.event || '');
                        message = String(obj.message || '');
                        if (event || message) {
                            display = `[${event || 'LOG'}] ${message || msgStr}`;
                        }
                    }
                } catch {
                    // keep raw
                }

                const type = /error|alert/i.test(event) ? 'error' : 'response';
                logToElement(monitorOutput, display, type);
            }
        } else if (topic.endsWith('/config/in')) { // Actually device sends to config/out usually? No, device receives on IN.
            // We need to listen to device responses? 
            // Currently firmware does not publish config back except on startup logs or serial.
            // Wait, config logic in firmware: it subscribes to CONFIG_IN.
            // We need a way to GET config.
            // Current firmware doesn't seem to support GET_CONFIG via MQTT, only Serial support sends it back.
            // We might be limited to blind configure or need to add GET_CONFIG support to firmware.
            // For now, let's assume blind configure or user manual entry.
        }
    }

    function handleHeartbeat(topic, msgStr) {
        try {
            const parts = topic.split('/'); // vending/ID/heartbeat
            const deviceId = parts[1];
            const data = JSON.parse(msgStr);

            devices.set(deviceId, {
                lastSeen: Date.now(),
                ...data
            });

            renderDevicesList();
        } catch (e) {
            console.error('HB Parse error', e);
        }
    }

    function renderDevicesList() {
        devicesList.innerHTML = '';
        devices.forEach((data, id) => {
            const card = document.createElement('div');
            card.className = `device-card ${selectedDevice === id ? 'selected' : ''}`;
            card.innerHTML = `
                <div style="font-weight:bold; margin-bottom:5px">
                    <ion-icon name="hardware-chip-outline" style="vertical-align:middle"></ion-icon> 
                    ${id}
                </div>
                <div style="font-size:12px; color:#888">
                    FW: ${data.firmware_version || 'Unknown'}<br>
                    IP: ${data.ip || 'Unknown'}<br>
                    WiFi: ${data.ssid || 'Unknown'} (${data.rssi} dBm)
                </div>
            `;
            card.addEventListener('click', () => selectDevice(id));
            devicesList.appendChild(card);
        });
    }

    function selectDevice(id) {
        selectedDevice = id;
        renderDevicesList(); // update selection style

        // Update header texts
        selectedDeviceConfigEls.forEach(el => (el.textContent = id));
        selectedDeviceMonitor.textContent = id;

        // Auto-fill and lock Device ID
        const devIdInput = document.getElementById(p + 'deviceId');
        if (devIdInput) {
            devIdInput.value = id;
            devIdInput.disabled = true;
            devIdInput.title = "Device ID change is locked in Online mode";
        }

        // Subscribe to logs
        window.electronAPI.mqttSubscribe(`vending/${id}/log/out`);
        window.electronAPI.mqttSubscribe(`vending/${id}/status/out`);
    }

    async function sendConfig(part) {
        if (!selectedDevice) return alert('Select a device');
        const label = part === 'basic' ? 'Basic' : 'Extra';
        if (!confirm(`Apply ${label} configuration to ${selectedDevice}?`)) return;

        // Get API secret for signing
        const apiSecret = document.getElementById(p + 'apiSecret')?.value || '';

        // Construct JSON payload
        const getVal = (id) => document.getElementById(p + id).value;
        const getChk = (id) => document.getElementById(p + id).checked;
        const getNum = (id) => Number(getVal(id));

        const applyModeEl = document.getElementById(p + (part === 'basic' ? 'basic_applyMode' : 'extra_applyMode'));
        const applyMode = (applyModeEl?.value || 'now').toLowerCase() === 'restart' ? 'restart' : 'now';

        const config = {
            apply: applyMode,
            nonce: makeNonce('cfg'),
            ts: Date.now()
        };

        if (part === 'basic') {
            // Network settings are optional and require allowRemoteNetworkConfig on device.
            const wifiSsid = getVal('wifiSsid').trim();
            const wifiPassword = getVal('wifiPassword');
            const mqttBroker = getVal('mqttBroker').trim();
            const mqttPortRaw = getVal('mqttPort').trim();
            const mqttUsername = getVal('mqttUsername').trim();
            const mqttPassword = getVal('mqttPassword');

            // WiFi: require password to update (prevents accidental partial updates).
            if (wifiPassword) {
                if (!wifiSsid) {
                    alert('WiFi SSID is required when setting WiFi password.');
                    return;
                }
                config.wifiSsid = wifiSsid;
                config.wifiPassword = wifiPassword;
            } else if (wifiSsid) {
                logToElement(monitorOutput, 'WiFi SSID is filled but password is empty — skipping WiFi update.', 'error');
            }
            if (mqttBroker) config.mqttBroker = mqttBroker;
            if (mqttPortRaw) {
                const port = Number(mqttPortRaw);
                if (!Number.isInteger(port) || port <= 0 || port > 65535) {
                    alert('MQTT Port must be 1-65535');
                    return;
                }
                config.mqttPort = port;
            }

            // MQTT auth: require password to update (prevents clearing stored password).
            if (mqttPassword) {
                if (!mqttUsername) {
                    alert('MQTT Username is required when setting MQTT password.');
                    return;
                }
                config.mqttUsername = mqttUsername;
                config.mqttPassword = mqttPassword;
            } else if (mqttUsername) {
                logToElement(monitorOutput, 'MQTT Username is filled but password is empty — skipping MQTT auth update.', 'error');
            }
        } else {
            Object.assign(config, {
                pricePerLiter: getNum('pricePerLiter'),
                sessionTimeout: getNum('sessionTimeout'),
                pulsesPerLiter: getNum('pulsesPerLiter'),
                tdsThreshold: getNum('tdsThreshold'),
                tdsTemperatureC: getNum('tdsTemperatureC'),
                tdsCalibrationFactor: getNum('tdsCalibrationFactor'),
                relayActiveHigh: getChk('relayActiveHigh'),
                cashPulseValue: getNum('cashPulseValue'),
                cashPulseGapMs: getNum('cashPulseGapMs'),
                paymentCheckInterval: getNum('paymentCheckInterval'),
                displayUpdateInterval: getNum('displayUpdateInterval'),
                tdsCheckInterval: getNum('tdsCheckInterval'),
                heartbeatInterval: getNum('heartbeatInterval'),
            });
        }

        // HIGH FIX: Add HMAC signature if API secret is provided
        if (apiSecret) {
            const signRes = await window.electronAPI.signPayload({
                type: 'config',
                deviceId: selectedDevice,
                payload: config,
                secret: apiSecret,
            });
            if (!signRes?.success) {
                alert('Signing failed: ' + (signRes?.message || 'unknown'));
                return;
            }
            config.sig = signRes.sig;
        } else {
            logToElement(monitorOutput, 'Warning: API Secret is empty (config will be unsigned)', 'error');
        }

        const topic = `vending/${selectedDevice}/config/in`;
        await window.electronAPI.mqttPublish({
            topic,
            message: JSON.stringify(config)
        });

        logToElement(monitorOutput, `${label} configuration sent to ${topic}`, 'command');
    }
}

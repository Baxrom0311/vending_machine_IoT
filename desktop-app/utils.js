export function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

export function logToElement(elementId, message, type = 'response') {
    const output = document.getElementById(elementId);
    if (!output) return;

    const logLine = document.createElement('div');
    logLine.className = `log-line ${type}`;
    logLine.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;

    // Style based on type
    if (type === 'command') logLine.style.color = '#569cd6';
    if (type === 'error') logLine.style.color = '#f44747';
    if (type === 'response') logLine.style.color = '#4ec9b0';

    output.appendChild(logLine);
    output.scrollTop = output.scrollHeight;

    // Limit lines
    if (output.children.length > 500) {
        output.removeChild(output.children[0]);
    }
}

export function maskCommandForLog(cmd) {
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

export function validateConfigInputs(prefix) {
    const p = prefix; // Alias
    const getVal = (id) => document.getElementById(p + id).value;
    const getNum = (id) => Number(getVal(id));

    const ssid = getVal('wifiSsid').trim();
    const password = getVal('wifiPassword');
    const broker = getVal('mqttBroker').trim();
    const port = getNum('mqttPort');
    const deviceId = getVal('deviceId').trim();

    if (!ssid) return 'WiFi SSID is required';
    if (!broker) return 'MQTT broker is required';
    if (!deviceId) return 'Device ID is required';
    if (!Number.isInteger(port) || port <= 0 || port > 65535) return 'MQTT port must be 1-65535';

    // ... (rest of validation)
    return null; // Return null if valid, error string if not
}

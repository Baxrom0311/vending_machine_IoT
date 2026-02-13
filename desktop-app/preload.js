const { contextBridge, ipcRenderer } = require('electron');

// Expose protected methods that allow the renderer process to use
// the ipcRenderer without exposing the entire object
contextBridge.exposeInMainWorld('electronAPI', {
    // Serial port operations
    scanPorts: () => ipcRenderer.invoke('scan-ports'),
    connectDevice: (portPath) => ipcRenderer.invoke('connect-device', portPath),
    disconnectDevice: () => ipcRenderer.invoke('disconnect-device'),
    sendCommand: (command) => ipcRenderer.invoke('send-command', command),
    selectFirmware: () => ipcRenderer.invoke('select-firmware'),
    flashFirmware: (payload) => ipcRenderer.invoke('flash-firmware', payload),

    // Serial data listeners
    onSerialData: (callback) => ipcRenderer.on('serial-data', (event, data) => callback(data)),
    onSerialError: (callback) => ipcRenderer.on('serial-error', (event, error) => callback(error)),
    onSerialClosed: (callback) => ipcRenderer.on('serial-closed', (event, message) => callback(message)),
    onFlashProgress: (callback) => ipcRenderer.on('flash-progress', (event, data) => callback(data)),
    onFlashDone: (callback) => ipcRenderer.on('flash-done', (event, data) => callback(data)),
    removeFlashListeners: () => {
        ipcRenderer.removeAllListeners('flash-progress');
        ipcRenderer.removeAllListeners('flash-done');
    },

    // MQTT operations
    mqttConnect: (config) => ipcRenderer.invoke('mqtt-connect', config),
    mqttSubscribe: (topic) => ipcRenderer.invoke('mqtt-subscribe', topic),
    mqttPublish: (payload) => ipcRenderer.invoke('mqtt-publish', payload),
    signPayload: (payload) => ipcRenderer.invoke('sign-payload', payload),

    // MQTT listeners
    onMqttStatus: (callback) => ipcRenderer.on('mqtt-status', (event, data) => callback(data)),
    onMqttMessage: (callback) => ipcRenderer.on('mqtt-message', (event, data) => callback(data)),

    // OTA operations
    startOtaServer: (file) => ipcRenderer.invoke('start-ota-server', file),
    stopOtaServer: () => ipcRenderer.invoke('stop-ota-server')
});

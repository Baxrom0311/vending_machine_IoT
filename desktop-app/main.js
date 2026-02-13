const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const { spawn, spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

let mainWindow;
let currentPort = null;
let parser = null;

function sendToRenderer(channel, payload) {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send(channel, payload);
    }
}

// Create main window
function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1200,
        height: 800,
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true,
            preload: path.join(__dirname, 'preload.js')
        },
        title: 'eWater Device Manager',
        backgroundColor: '#1e1e1e'
    });

    mainWindow.loadFile(path.join(__dirname, 'index.html'));

    // Open DevTools in development
    // mainWindow.webContents.openDevTools();
}

app.whenReady().then(() => {
    createWindow();

    app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) {
            createWindow();
        }
    });
});

app.on('window-all-closed', () => {
    if (currentPort && currentPort.isOpen) {
        currentPort.close();
    }
    if (process.platform !== 'darwin') {
        app.quit();
    }
});

// ============================================
// IPC HANDLERS - Serial Communication
// ============================================

// Scan for available serial ports
ipcMain.handle('scan-ports', async () => {
    try {
        const ports = await SerialPort.list();
        return ports.map(port => ({
            path: port.path,
            manufacturer: port.manufacturer || 'Unknown',
            serialNumber: port.serialNumber || '',
            vendorId: port.vendorId || '',
            productId: port.productId || ''
        }));
    } catch (error) {
        console.error('Error scanning ports:', error);
        return [];
    }
});

// Connect to device
ipcMain.handle('connect-device', async (event, portPath) => {
    try {
        if (currentPort && currentPort.isOpen) {
            await new Promise(resolve => currentPort.close(resolve));
            currentPort = null;
            parser = null;
        }

        currentPort = new SerialPort({
            path: portPath,
            baudRate: 115200,
            autoOpen: false
        });

        await new Promise((resolve, reject) => {
            currentPort.open(err => (err ? reject(err) : resolve()));
        });

        parser = currentPort.pipe(new ReadlineParser({ delimiter: '\n' }));

        // Forward serial data to renderer
        parser.on('data', (data) => {
            sendToRenderer('serial-data', data.trim());
        });

        currentPort.on('error', (err) => {
            sendToRenderer('serial-error', err.message);
        });

        currentPort.on('close', () => {
            sendToRenderer('serial-closed', 'Serial port closed');
        });

        return { success: true, message: 'Connected' };
    } catch (error) {
        return { success: false, message: error.message };
    }
});

// Disconnect from device
ipcMain.handle('disconnect-device', async () => {
    try {
        if (currentPort && currentPort.isOpen) {
            await new Promise(resolve => currentPort.close(resolve));
            currentPort = null;
            parser = null;
            return { success: true };
        }
        return { success: false, message: 'No active connection' };
    } catch (error) {
        return { success: false, message: error.message };
    }
});

// Send command to device
ipcMain.handle('send-command', async (event, command) => {
    try {
        if (!currentPort || !currentPort.isOpen) {
            return { success: false, message: 'Not connected' };
        }

        currentPort.write(command + '\n');
        return { success: true };
    } catch (error) {
        return { success: false, message: error.message };
    }
});

// ============================================
// IPC HANDLERS - Signing (HMAC-SHA256)
// Mirrors firmware canonicalization (src_esp32_main/mqtt_handler.cpp)
// ============================================
function isPresentForCanonical(v) {
    if (v === undefined || v === null) return false;
    if (typeof v === 'number' && !Number.isFinite(v)) return false;
    return true;
}

function buildCanonicalPayload(type, payload, deviceId) {
    const canonical = {};

    const add = (key) => {
        if (isPresentForCanonical(payload[key])) {
            canonical[key] = payload[key];
        }
    };

    const t = String(type || '').toLowerCase();

    if (t === 'payment') {
        if (!isPresentForCanonical(payload.amount)) {
            throw new Error('PAYMENT missing amount');
        }
        canonical.amount = payload.amount;
        add('source');
        add('transaction_id');
        add('nonce');
        add('user_id');
        add('ts');
    } else if (t === 'config') {
        add('apply');
        add('deviceId');
        add('wifiSsid');
        add('wifiPassword');
        add('mqttBroker');
        add('mqttPort');
        add('mqttUsername');
        add('mqttPassword');
        add('pricePerLiter');
        add('sessionTimeout');
        add('freeWaterCooldown');
        add('freeWaterAmount');
        add('pulsesPerLiter');
        add('tdsThreshold');
        add('tdsTemperatureC');
        add('tdsCalibrationFactor');
        add('enableFreeWater');
        add('relayActiveHigh');
        add('relay_active_high');
        add('cashPulseValue');
        add('cashPulseGapMs');
        add('paymentCheckInterval');
        add('displayUpdateInterval');
        add('tdsCheckInterval');
        add('heartbeatInterval');
        add('enablePowerSave');
        add('deepSleepStartHour');
        add('deepSleepEndHour');
        add('transaction_id');
        add('nonce');
        add('ts');
    } else if (t === 'command') {
        add('action');
        add('pricePerLiter');
        add('threshold');
        add('tdsThreshold');
        add('duration');
        add('reason');
        add('transaction_id');
        add('nonce');
        add('ts');
    } else if (t === 'ota') {
        add('firmware_url');
        add('transaction_id');
        add('nonce');
        add('ts');
    } else {
        throw new Error(`Unsupported sign type: ${type}`);
    }

    canonical.device_id = deviceId;
    return JSON.stringify(canonical);
}

ipcMain.handle('sign-payload', async (event, req) => {
    try {
        const { type, deviceId, payload, secret } = req || {};

        if (!type) return { success: false, message: 'Missing type' };
        if (!deviceId || typeof deviceId !== 'string' || !deviceId.trim()) {
            return { success: false, message: 'Missing deviceId' };
        }
        if (!payload || typeof payload !== 'object') {
            return { success: false, message: 'Missing payload' };
        }
        if (!secret || typeof secret !== 'string') {
            return { success: false, message: 'Missing secret' };
        }

        const canonical = buildCanonicalPayload(type, payload, deviceId.trim());
        const sig = crypto.createHmac('sha256', secret).update(canonical, 'utf8').digest('hex');

        return { success: true, sig, canonical };
    } catch (error) {
        return { success: false, message: error.message };
    }
});

function getPlatformioPython() {
    const home = app.getPath('home');
    if (process.platform === 'win32') {
        const p = path.join(home, '.platformio', 'penv', 'Scripts', 'python.exe');
        return fs.existsSync(p) ? p : null;
    }
    const p = path.join(home, '.platformio', 'penv', 'bin', 'python');
    return fs.existsSync(p) ? p : null;
}

function pythonHasPySerial(python) {
    const check = spawnSync(python, ['-c', 'import serial; print(serial.__version__)']);
    return check.status === 0;
}

function findPython() {
    const candidates = [];

    const pioPython = getPlatformioPython();
    if (pioPython) candidates.push(pioPython);

    if (process.env.PYTHON) candidates.push(process.env.PYTHON);

    if (process.platform === 'win32') {
        candidates.push('python', 'python3');
    } else {
        candidates.push('python3', 'python');
    }

    for (const candidate of candidates) {
        // Validate executable exists (for full paths) or resolves (for commands)
        const version = spawnSync(candidate, ['--version']);
        if (version.status !== 0) continue;

        // esptool requires pyserial
        if (!pythonHasPySerial(candidate)) continue;

        return candidate;
    }

    return null;
}

function findEsptoolScript() {
    const envPath = process.env.ESPTOOL_PATH;
    if (envPath && fs.existsSync(envPath)) {
        return envPath;
    }
    const home = app.getPath('home');
    const pioEsptool = path.join(home, '.platformio', 'packages', 'tool-esptoolpy', 'esptool.py');
    if (fs.existsSync(pioEsptool)) {
        return pioEsptool;
    }
    return null;
}

// File dialog for firmware selection
ipcMain.handle('select-firmware', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
        properties: ['openFile'],
        filters: [
            { name: 'Firmware', extensions: ['bin'] },
            { name: 'All Files', extensions: ['*'] }
        ]
    });

    if (!result.canceled && result.filePaths.length > 0) {
        return { success: true, path: result.filePaths[0] };
    }
    return { success: false };
});

// Flash firmware using esptool.py
ipcMain.handle('flash-firmware', async (event, payload) => {
    try {
        const { portPath, firmwarePath, baud, offset } = payload || {};
        if (!portPath || !firmwarePath) {
            return { success: false, message: 'Missing port or firmware path' };
        }
        if (!fs.existsSync(firmwarePath)) {
            return { success: false, message: 'Firmware file not found' };
        }

        if (currentPort && currentPort.isOpen) {
            await new Promise(resolve => currentPort.close(resolve));
        }

        const python = findPython();
        if (!python) {
            return { success: false, message: 'Python not found. Install Python 3 first.' };
        }

        let args = [];
        const esptoolPath = findEsptoolScript();
        if (esptoolPath) {
            args = [esptoolPath];
        } else {
            // Try python -m esptool
            const check = spawnSync(python, ['-m', 'esptool', '--version']);
            if (check.status !== 0) {
                return { success: false, message: 'esptool not found. Install PlatformIO or esptool.' };
            }
            args = ['-m', 'esptool'];
        }

        const baudRate = Number(baud) || 460800;
        const flashOffset = typeof offset === 'string' && offset.trim() ? offset.trim() : '0x10000';
        args = args.concat([
            '--chip', 'esp32',
            '--port', portPath,
            '--baud', String(baudRate),
            '--before', 'default_reset',
            '--after', 'hard_reset',
            'write_flash', '-z',
            flashOffset, firmwarePath
        ]);

        return await new Promise((resolve) => {
            const child = spawn(python, args);

            const handleOutput = (data) => {
                const text = data.toString();
                const match = text.match(/(\d+)\s*%/);
                const percent = match ? Math.min(100, Number(match[1])) : undefined;
                sendToRenderer('flash-progress', { percent, message: text.trim() });
            };

            child.stdout.on('data', handleOutput);
            child.stderr.on('data', handleOutput);

            child.on('error', (err) => {
                const message = `Failed to start esptool: ${err.message}`;
                sendToRenderer('flash-done', { success: false, message });
                resolve({ success: false, message });
            });

            child.on('close', (code) => {
                if (code === 0) {
                    sendToRenderer('flash-done', { success: true });
                    resolve({ success: true });
                } else {
                    const message = `esptool exited with code ${code}`;
                    sendToRenderer('flash-done', { success: false, message });
                    resolve({ success: false, message });
                }
            });
        });
    } catch (error) {
        return { success: false, message: error.message };
    }
});

console.log('eWater Device Manager started');

// ============================================
// MQTT & OTA HANDLERS
// ============================================
const mqtt = require('mqtt');
const express = require('express');
const http = require('http');

let mqttClient = null;
let otaServer = null;
let otaServerPort = 0;

// MQTT Connect
ipcMain.handle('mqtt-connect', async (event, config) => {
    return new Promise((resolve) => {
        if (mqttClient) {
            mqttClient.removeAllListeners();
            mqttClient.end(true);
            mqttClient = null;
        }

        const { host, port, username, password } = config || {};
        if (!host || !port) {
            resolve({ success: false, message: 'Missing MQTT host or port' });
            return;
        }
        const protocol = 'mqtt'; // Using TCP not WS
        const url = `${protocol}://${host}:${port}`;

        const clientId = 'eWater_Desktop_' + Math.random().toString(16).substr(2, 8);
        console.log(`Connecting to ${url} as ${clientId}...`);

        mqttClient = mqtt.connect(url, {
            clientId,
            username: username || undefined,
            password: password || undefined,
            reconnectPeriod: 5000,
            connectTimeout: 10000,
            protocolVersion: 4 // Force MQTT 3.1.1 (safer for compatibility)
        });

        let resolved = false;
        const safeResolve = (payload) => {
            if (resolved) return;
            resolved = true;
            resolve(payload);
        };

        const connectTimeout = setTimeout(() => {
            if (!mqttClient.connected) {
                // Don't close, let it retry, but tell renderer it's pending
                sendToRenderer('mqtt-status', { status: 'error', message: 'Connection pending/timeout' });
                safeResolve({ success: false, message: 'Connection pending/timeout' });
            }
        }, 11000);

        mqttClient.on('connect', () => {
            clearTimeout(connectTimeout);
            sendToRenderer('mqtt-status', { status: 'connected' });
            safeResolve({ success: true });
        });

        mqttClient.on('reconnect', () => {
            sendToRenderer('mqtt-status', { status: 'reconnecting' });
        });

        mqttClient.on('offline', () => {
            sendToRenderer('mqtt-status', { status: 'offline' });
        });

        mqttClient.on('close', () => {
            sendToRenderer('mqtt-status', { status: 'disconnected' });
        });

        mqttClient.on('error', (err) => {
            sendToRenderer('mqtt-status', { status: 'error', message: err.message });
            clearTimeout(connectTimeout);
            safeResolve({ success: false, message: err.message });
        });

        mqttClient.on('message', (topic, message) => {
            sendToRenderer('mqtt-message', { topic, message: message.toString() });
        });
    });
});

ipcMain.handle('mqtt-subscribe', async (event, topic) => {
    if (!mqttClient) {
        return { success: false, message: 'MQTT not connected' };
    }
    if (!topic) {
        return { success: false, message: 'Missing topic' };
    }

    return await new Promise((resolve) => {
        mqttClient.subscribe(topic, { qos: 0 }, (err, granted) => {
            if (err) {
                resolve({ success: false, message: err.message });
            } else {
                resolve({ success: true, granted });
            }
        });
    });
});

ipcMain.handle('mqtt-publish', async (event, payload) => {
    if (!mqttClient) {
        return { success: false, message: 'MQTT not connected' };
    }
    const { topic, message, qos, retain } = payload || {};
    if (!topic) {
        return { success: false, message: 'Missing topic' };
    }

    return await new Promise((resolve) => {
        mqttClient.publish(topic, message ?? '', { qos: qos ?? 0, retain: !!retain }, (err) => {
            if (err) {
                resolve({ success: false, message: err.message });
            } else {
                resolve({ success: true });
            }
        });
    });
});

// OTA Server Start
ipcMain.handle('start-ota-server', async (event, filePath) => {
    try {
        if (!filePath) {
            return { success: false, message: 'Missing firmware file path' };
        }
        if (!fs.existsSync(filePath)) {
            return { success: false, message: 'Firmware file not found' };
        }

        if (otaServer) {
            otaServer.close();
            otaServer = null;
            otaServerPort = 0;
        }

        const fileName = path.basename(filePath);
        const appServer = express();
        const encodedName = encodeURIComponent(fileName);

        appServer.get(`/${encodedName}`, (req, res) => {
            res.sendFile(filePath);
        });

        appServer.get('/', (req, res) => {
            res.send(`eWater OTA server running. Download: /${encodedName}`);
        });

        otaServer = http.createServer(appServer);

        await new Promise((resolve, reject) => {
            otaServer.once('error', reject);
            otaServer.listen(0, '0.0.0.0', () => resolve());
        });

        const address = otaServer.address();
        otaServerPort = typeof address === 'object' && address ? address.port : 0;

        // Find non-internal IPv4
        const nets = os.networkInterfaces();
        let ip = '127.0.0.1';
        let found = false;
        for (const name of Object.keys(nets)) {
            for (const net of nets[name]) {
                if (net.family === 'IPv4' && !net.internal) {
                    ip = net.address;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        const url = `http://${ip}:${otaServerPort}/${encodedName}`;
        console.log(`OTA Server started at ${url}`);
        return { success: true, url };
    } catch (error) {
        return { success: false, message: error.message };
    }
});

// OTA Server Stop
ipcMain.handle('stop-ota-server', async () => {
    if (otaServer) {
        otaServer.close();
        otaServer = null;
        otaServerPort = 0;
    }
    return { success: true };
});

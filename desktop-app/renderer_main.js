import { generateBasicConfigForm, generateExtraConfigForm } from './config_form.js';
import { setupSerial } from './renderer_serial.js';
import { setupOnline } from './renderer_online.js';

document.addEventListener('DOMContentLoaded', () => {
    const CONTROLLER_STORAGE_KEY = 'ewater_controller_mode';

    // 1. Render Forms
    document.getElementById('serialBasicConfigFormContainer').innerHTML = generateBasicConfigForm('serial_');
    document.getElementById('serialExtraConfigFormContainer').innerHTML = generateExtraConfigForm('serial_');
    document.getElementById('onlineBasicConfigFormContainer').innerHTML = generateBasicConfigForm('online_');
    document.getElementById('onlineExtraConfigFormContainer').innerHTML = generateExtraConfigForm('online_');

    // 2. Initialize Logic
    setupSerial('serial_');
    setupOnline('online_');

    // 3. Navigation Logic
    const navItems = document.querySelectorAll('.nav-item');
    const views = document.querySelectorAll('.view-container');

    const activateView = (viewId) => {
        const targetNav = Array.from(navItems).find(n => n.dataset.view === viewId);
        if (targetNav?.disabled) return;

        navItems.forEach(n => n.classList.remove('active'));
        if (targetNav) targetNav.classList.add('active');

        views.forEach(v => v.classList.remove('active'));
        const view = document.getElementById(viewId);
        if (view) view.classList.add('active');
    };

    navItems.forEach(item => {
        item.addEventListener('click', () => {
            if (item.disabled) return;
            activateView(item.dataset.view);
        });
    });

    // 4. Inner Tab Logic (Shared)
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const parent = btn.closest('.view-container');
            const targetId = btn.dataset.tab;

            // Deactivate siblings
            parent.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            parent.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));

            // Activate target
            btn.classList.add('active');
            document.getElementById(targetId).classList.add('active');
        });
    });

    const activateTab = (viewId, tabId) => {
        const view = document.getElementById(viewId);
        if (!view) return;

        view.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        view.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));

        const btn = view.querySelector(`.tab-btn[data-tab="${tabId}"]`);
        const pane = document.getElementById(tabId);
        if (btn) btn.classList.add('active');
        if (pane) pane.classList.add('active');
    };

    // 5. Controller Mode (Main vs Payment)
    const controllerSelect = document.getElementById('controllerSelect');
    const onlineNavBtn = document.querySelector('.nav-item[data-view="online-view"]');
    const serialTitleEl = document.querySelector('#serial-view .view-header h2');
    const onlineTitleEl = document.querySelector('#online-view .view-header h2');

    const serialConfigTabBtns = [
        document.querySelector('#serial-view .tab-btn[data-tab="serial-config-basic"]'),
        document.querySelector('#serial-view .tab-btn[data-tab="serial-config-extra"]'),
    ].filter(Boolean);
    const serialConfigPanes = [
        document.getElementById('serial-config-basic'),
        document.getElementById('serial-config-extra'),
    ].filter(Boolean);

    const applyControllerMode = (mode) => {
        const normalized = mode === 'payment' ? 'payment' : 'main';
        localStorage.setItem(CONTROLLER_STORAGE_KEY, normalized);

        const isMain = normalized === 'main';

        if (serialTitleEl) {
            serialTitleEl.textContent = isMain ? 'Serial Setup — Main ESP32' : 'Serial Setup — Payment ESP32';
        }
        if (onlineTitleEl) {
            onlineTitleEl.textContent = 'Online Management — Main ESP32';
        }

        if (onlineNavBtn) {
            onlineNavBtn.disabled = !isMain;
            onlineNavBtn.classList.toggle('disabled', !isMain);
            onlineNavBtn.title = !isMain ? 'Online setup is available only for Main ESP32' : '';
        }

        serialConfigTabBtns.forEach((btn) => {
            btn.style.display = isMain ? '' : 'none';
        });
        serialConfigPanes.forEach((pane) => {
            pane.style.display = isMain ? '' : 'none';
        });

        if (!isMain) {
            // If user is on Online view, force back to Serial.
            if (document.getElementById('online-view')?.classList.contains('active')) {
                activateView('serial-view');
            }

            // If any config tab is active, move to firmware tab.
            const configActive = serialConfigPanes.some(p => p.classList.contains('active')) ||
                serialConfigTabBtns.some(b => b.classList.contains('active'));
            if (configActive) activateTab('serial-view', 'serial-firmware');
        }
    };

    if (controllerSelect) {
        const saved = localStorage.getItem(CONTROLLER_STORAGE_KEY) || 'main';
        controllerSelect.value = saved === 'payment' ? 'payment' : 'main';
        controllerSelect.addEventListener('change', () => applyControllerMode(controllerSelect.value));
        applyControllerMode(controllerSelect.value);
    } else {
        applyControllerMode('main');
    }
});

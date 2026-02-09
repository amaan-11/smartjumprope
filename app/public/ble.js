// ===== BLE UUIDs (choose once, match ESP32 firmware) =====
const JR_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const JR_CTRL_UUID    = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const JR_DATA_UUID    = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

let device, server, ctrlChar, dataChar;

function setStatus(msg) {
    const el = document.getElementById("bleStatus");
    if (el) el.textContent = msg;
}

function enableControls(connected) {
    const start = document.getElementById("btnStart");
    const stop = document.getElementById("btnStop");
    if (start) start.disabled = !connected;
    if (stop) stop.disabled = !connected;
}

async function connectBLE() {
    if (!navigator.bluetooth) {
        setStatus("Web Bluetooth not supported in this browser.");
        return;
    }

    setStatus("Requesting device...");
    device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [JR_SERVICE_UUID] }],
    });

    device.addEventListener("gattserverdisconnected", () => {
        setStatus("Disconnected");
        enableControls(false);
    });

    setStatus("Connecting...");
    server = await device.gatt.connect();

    const service = await server.getPrimaryService(JR_SERVICE_UUID);
    ctrlChar = await service.getCharacteristic(JR_CTRL_UUID);
    dataChar = await service.getCharacteristic(JR_DATA_UUID);

    await dataChar.startNotifications();
    dataChar.addEventListener("characteristicvaluechanged", onData);

    setStatus("Connected");
    enableControls(true);
}

function onData(event) {
    const v = event.target.value; // DataView

    // Example packet v1 (12 bytes):
    // 0: u32 timestamp_ms
    // 4: u32 jump_count
    // 8: u8  heart_rate_bpm
    // 9: u16 accel_mag_mg
    // 11: u8 flags
    const ts = v.getUint32(0, true);
    const jumps = v.getUint32(4, true);
    const hr = v.getUint8(8);
    const accelMag = v.getUint16(9, true);
    const flags = v.getUint8(11);

    // Replace these with your actual DOM element IDs in data.html
    const jumpsEl = document.getElementById("jumpCount");
    const hrEl = document.getElementById("heartRate");
    const accelEl = document.getElementById("accelMag");

    if (jumpsEl) jumpsEl.textContent = String(jumps);
    if (hrEl) hrEl.textContent = hr === 0 ? "-" : String(hr);
    if (accelEl) accelEl.textContent = String(accelMag);

    console.log({ ts, jumps, hr, accelMag, flags });
}

async function startStreaming() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x01]));
}

async function stopStreaming() {
    if (!ctrlChar) return;
    await ctrlChar.writeValue(Uint8Array.from([0x00]));
}

function initBLE() {
    const btnConnect = document.getElementById("btnConnect");
    const btnStart = document.getElementById("btnStart");
    const btnStop = document.getElementById("btnStop");

    if (btnConnect) {
        btnConnect.addEventListener("click", () => connectBLE().catch(e => setStatus(String(e))));
        console.log(`connecting...`);
    }
    if (btnStart) btnStart.addEventListener("click", () => startStreaming().catch(e => setStatus(String(e))));

    if (btnStop) {
        btnStop.addEventListener("click", async () => {
            try {
                await stopStreaming(); 

                const jump_counts = document.getElementById('jumpCount');
                const hr = document.getElementById('heartRate');
                const acceleration = document.getElementById('accelMag');

                if (jump_counts) jump_counts.value = "0";
                if (hr) hr.value = "-";
                if (acceleration) acceleration.value = "0";
            }
            catch (e) {
                setStatus(String(e));
            }

        });
    }
    /*if (btnStop) {
        btnStop.addEventListener("click", () => stopStreaming().catch(e => setStatus(String(e))));
        
    }*/

    setStatus("Disconnected");
    enableControls(false);
}

document.addEventListener("DOMContentLoaded", initBLE);

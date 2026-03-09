// ===== BLE UUIDs =====
const JR_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const JR_CTRL_UUID    = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const JR_DATA_UUID    = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

let device, server, ctrlChar, dataChar;
let hrRunning = false;  // tracks HR task state so the toggle button stays in sync

function setStatus(msg) {
  const el = document.getElementById("bleStatus");
  if (el) el.textContent = msg;
}

function enableControls(connected) {
  const ids = ["btnStart", "btnStop", "btnToggleHR"];
  ids.forEach(id => {
    const el = document.getElementById(id);
    if (el) el.disabled = !connected;
  });
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
    // Reset HR toggle state on disconnect
    hrRunning = false;
    const btn = document.getElementById("btnToggleHR");
    if (btn) btn.textContent = "Start HR";
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
  const ts      = v.getUint32(0, true);
  const jumps   = v.getUint32(4, true);
  const hr      = v.getUint8(8);
  const accelMag = v.getUint16(9, true);
  const flags   = v.getUint8(11);

  if (window.workoutOnLiveData) {
    window.workoutOnLiveData({ jumps, hr });
  }

  const jumpsEl = document.getElementById("jumpCount");
  const hrEl    = document.getElementById("heartRate");
  const accelEl = document.getElementById("accelMag");
  if (jumpsEl) jumpsEl.textContent = String(jumps);
  if (hrEl)    hrEl.textContent    = hr === 0 ? "-" : String(hr);
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

// Toggles HR measurement on the ESP32 on/off with a single button.
async function toggleHR() {
  if (!ctrlChar) return;
  hrRunning = !hrRunning;
  const cmd = hrRunning ? 0x02 : 0x03;
  await ctrlChar.writeValue(Uint8Array.from([cmd]));

  const btn = document.getElementById("btnToggleHR");
  if (btn) btn.textContent = hrRunning ? "Stop HR" : "Start HR";
  setStatus(hrRunning ? "HR measurement started" : "HR measurement stopped");
}

function initBLE() {
  const btnConnect  = document.getElementById("btnConnect");
  const btnStart    = document.getElementById("btnStart");
  const btnStop     = document.getElementById("btnStop");
  const btnToggleHR = document.getElementById("btnToggleHR");

  if (btnConnect)  btnConnect.addEventListener("click",  () => connectBLE().catch(e    => setStatus(String(e))));
  if (btnStart)    btnStart.addEventListener("click",    () => startStreaming().catch(e => setStatus(String(e))));
  if (btnStop)     btnStop.addEventListener("click",     () => stopStreaming().catch(e  => setStatus(String(e))));
  if (btnToggleHR) btnToggleHR.addEventListener("click", () => toggleHR().catch(e      => setStatus(String(e))));

  setStatus("Disconnected");
  enableControls(false);
}

document.addEventListener("DOMContentLoaded", initBLE);
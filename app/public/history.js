function formatDuration(ms) {
    const s = Math.floor(ms / 1000);
    const m = Math.floor(s / 60);
    const remS = s % 60;
    if (m <= 0) return `${remS}s`;
    return `${m}m ${remS}s`;
}

function formatDate(iso) {
    try {
        return new Date(iso).toLocaleString();
    } catch {
        return iso;
    }
}

function cell(text) {
    const td = document.createElement("td");
    td.textContent = text;
    return td;
}

async function fetchHistory() {
    const res = await fetch("/api/workouts");
    const json = await res.json().catch(() => ({}));

    if (!res.ok || !json.ok) {
        throw new Error(json.error || `HTTP ${res.status}`);
    }
    return json.workouts || [];
}

async function renderHistory() {
    const table = document.getElementById("historyTable");
    if (!table) return;

    const tbody = table.querySelector("tbody");
    tbody.innerHTML = "";

    const workouts = await fetchHistory();

    for (const w of workouts) {
        const tr = document.createElement("tr");

        tr.appendChild(cell(String(w.id)));
        tr.appendChild(cell(formatDate(w.start_time)));
        tr.appendChild(cell(formatDuration(w.duration_ms)));
        tr.appendChild(cell(String(w.jump_count)));

        tr.appendChild(cell(w.avg_heart_rate_bpm == null ? "-" : String(w.avg_heart_rate_bpm)));
        tr.appendChild(cell(w.max_heart_rate_bpm == null ? "-" : String(w.max_heart_rate_bpm)));

        tr.appendChild(cell(w.device_name || "-"));

        tbody.appendChild(tr);
    }
}

function initHistory() {
    const btn = document.getElementById("btnRefreshHistory");
    if (btn) {
        btn.addEventListener("click", () => {
            renderHistory().catch(console.error);
        });
    }

    renderHistory().catch(console.error);
}

document.addEventListener("DOMContentLoaded", initHistory);
const $ = (sel) => document.querySelector(sel);

const loginScreen = $('#login-screen');
const appRoot = $('#app');

// ── Auth ──────────────────────────────────────────────────────────────

async function checkAuth() {
  const res = await fetch('/api/auth/status');
  const data = await res.json();
  if (data.authenticated) {
    loginScreen.classList.add('hidden');
    appRoot.classList.remove('hidden');
    boot();
  } else {
    appRoot.classList.add('hidden');
    loginScreen.classList.remove('hidden');
  }
}

$('#login-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const password = $('#login-password').value;
  const errEl = $('#login-error');
  errEl.textContent = '';
  const res = await fetch('/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ password }),
  });
  if (res.ok) {
    window.location.reload();
  } else {
    errEl.textContent = "That password's not it.";
  }
});

$('#logout-link').addEventListener('click', async (e) => {
  e.preventDefault();
  await fetch('/logout', { method: 'POST' });
  window.location.reload();
});

async function api(path, opts = {}) {
  const res = await fetch(path, opts);
  if (res.status === 401) {
    window.location.reload();
    throw new Error('unauthorized');
  }
  return res;
}

// ── Desk unit clock ──────────────────────────────────────────────────

function tickClock() {
  const now = new Date();
  let hh = now.getHours() % 12;
  if (hh === 0) hh = 12;
  const mm = String(now.getMinutes()).padStart(2, '0');
  const ss = String(now.getSeconds()).padStart(2, '0');
  const ampm = now.getHours() < 12 ? 'AM' : 'PM';
  $('#clock').textContent = `${hh}:${mm}:${ss} ${ampm}`;
  $('#date').textContent = now.toLocaleDateString(undefined, {
    weekday: 'short', month: 'short', day: 'numeric',
  });
}

// ── Background theme ──────────────────────────────────────────────────

async function loadSettings() {
  try {
    const res = await api('/api/settings');
    const data = await res.json();
    applyTheme(data.bg_theme);
    applyVolume(data.volume);
    applyWifiStatus(data.wifi_apply_status, data.pending_wifi_ssid);
  } catch {
    // fall back to whatever the screen already shows
  }
}

function applyTheme(theme) {
  $('#screen').dataset.theme = theme;
  for (const btn of document.querySelectorAll('#theme-picker button')) {
    btn.classList.toggle('active', btn.dataset.theme === theme);
  }
}

$('#theme-picker').addEventListener('click', async (e) => {
  const btn = e.target.closest('button');
  if (!btn) return;
  const theme = btn.dataset.theme;
  applyTheme(theme); // optimistic
  try {
    await api('/api/settings', {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ bg_theme: theme }),
    });
  } catch {
    loadSettings(); // revert to server truth on failure
  }
});

// ── Volume ───────────────────────────────────────────────────────────

function applyVolume(volume) {
  const pct = Math.round(volume * 100);
  $('#volume-slider').value = pct;
  $('#volume-value').textContent = `${pct}%`;
}

let volumeSaveTimer = null;
$('#volume-slider').addEventListener('input', (e) => {
  const pct = Number(e.target.value);
  $('#volume-value').textContent = `${pct}%`;
  clearTimeout(volumeSaveTimer);
  volumeSaveTimer = setTimeout(async () => {
    try {
      await api('/api/settings', {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ volume: pct / 100 }),
      });
    } catch {
      loadSettings(); // revert to server truth on failure
    }
  }, 300);
});

// ── Network (WiFi) ──────────────────────────────────────────────────

// The server only echoes pending_wifi_ssid while status is "applying" — it
// clears it the moment the device acks. Remember what we submitted so the
// applied/failed message can still name the network.
let lastSubmittedSsid = '';
let wifiPollTimer = null;

const WIFI_STATUS_TEXT = {
  applying: (ssid) => `Applying "${ssid}"…`,
  applied: (ssid) => `Connected to "${ssid}"`,
  failed: (ssid) => `Failed to join "${ssid}" — reverted to the previous network`,
};

function applyWifiStatus(status, pendingSsid) {
  const el = $('#wifi-status');
  const ssid = pendingSsid || lastSubmittedSsid;
  if (!status || status === 'none' || !WIFI_STATUS_TEXT[status]) {
    el.classList.add('hidden');
    clearInterval(wifiPollTimer);
    wifiPollTimer = null;
    return;
  }
  el.dataset.status = status;
  el.classList.remove('hidden');
  $('#wifi-status-text').textContent = WIFI_STATUS_TEXT[status](ssid);

  if (status === 'applying' && !wifiPollTimer) {
    wifiPollTimer = setInterval(loadSettings, 3000);
  } else if (status !== 'applying' && wifiPollTimer) {
    clearInterval(wifiPollTimer);
    wifiPollTimer = null;
  }
}

$('#wifi-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const ssid = $('#wifi-ssid').value.trim();
  const password = $('#wifi-password').value;
  if (!ssid) return;
  lastSubmittedSsid = ssid;
  try {
    await api('/api/settings/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password }),
    });
    $('#wifi-password').value = '';
    loadSettings();
  } catch {
    // leave the form as-is so the user can retry
  }
});

$('#wifi-status-dismiss').addEventListener('click', async (e) => {
  e.preventDefault();
  try {
    await api('/api/settings/wifi/dismiss', { method: 'POST' });
  } catch {
    // ignore — next loadSettings() will resync
  }
  loadSettings();
});

function updateDeskStatus(todos) {
  const pending = todos.filter((t) => !t.done);
  const el = $('#desk-status');
  el.textContent = pending.length === 0
    ? 'no tasks pending'
    : `${pending.length} pending — ${pending[0].text}`;
}

// ── Search + queue ───────────────────────────────────────────────────

function formatDuration(sec) {
  if (!sec && sec !== 0) return '';
  const m = Math.floor(sec / 60);
  const s = String(sec % 60).padStart(2, '0');
  return `${m}:${s}`;
}

function renderResults(results) {
  const container = $('#results');
  container.innerHTML = '';
  if (results.length === 0) {
    container.innerHTML = '<div class="empty-note">No results. Try a different search.</div>';
    return;
  }
  for (const r of results) {
    const row = document.createElement('div');
    row.className = 'result-row';
    row.innerHTML = `
      <img src="${r.thumbnail_url || ''}" alt="" loading="lazy">
      <div class="result-meta">
        <div class="result-title">${escapeHtml(r.title)}</div>
        <div class="result-sub">${escapeHtml(r.channel || '')} · ${formatDuration(r.duration)}</div>
      </div>
      <button class="play-btn" data-video-id="${r.video_id}">Play on Deskbot</button>
    `;
    container.appendChild(row);
  }
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str || '';
  return div.innerHTML;
}

$('#search-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const q = $('#search-input').value.trim();
  if (!q) return;
  const container = $('#results');
  container.innerHTML = '<div class="empty-note">Searching…</div>';
  try {
    const res = await api(`/api/search?q=${encodeURIComponent(q)}`);
    const data = await res.json();
    renderResults(data.results);
  } catch {
    container.innerHTML = '<div class="empty-note">Search failed. Try again.</div>';
  }
});

$('#results').addEventListener('click', async (e) => {
  const btn = e.target.closest('.play-btn');
  if (!btn) return;
  const videoId = btn.dataset.videoId;
  btn.disabled = true;
  const originalText = btn.textContent;
  btn.textContent = 'Queuing…';
  try {
    const res = await api('/api/queue', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ video_id: videoId }),
    });
    if (res.status === 409) {
      btn.textContent = 'Deskbot is busy';
      setTimeout(() => { btn.textContent = originalText; btn.disabled = false; }, 2500);
      return;
    }
    if (!res.ok) throw new Error('queue failed');
    btn.textContent = 'Queued';
    pollNowPlaying();
    setTimeout(() => { btn.textContent = originalText; btn.disabled = false; }, 2500);
  } catch {
    btn.textContent = 'Failed — retry';
    btn.disabled = false;
  }
});

// ── Now playing ──────────────────────────────────────────────────────

const STATUS_LABELS = {
  queued: 'Queued',
  downloading: 'Downloading',
  encoding: 'Encoding',
  ready: 'Ready',
  playing: 'Playing',
  error: 'Error',
};

let currentJobId = null;

async function pollNowPlaying() {
  try {
    // Unfiltered (not ?active=1): an "error" job isn't in the active-status
    // list, so filtering to active-only made failures flash back to "Idle"
    // on the very next poll (2.5s later) before you could ever read why.
    // Fetching the single most recent job regardless of status keeps a
    // failure visible until you Retry it or it ages out.
    const res = await api('/api/jobs');
    const jobs = await res.json();
    const pill = $('#now-status');
    const title = $('#now-title');
    const actions = $('#now-actions');
    const job = jobs[0];
    if (!job || job.status === 'done') {
      currentJobId = null;
      pill.dataset.status = 'idle';
      pill.textContent = 'Idle';
      title.textContent = 'Nothing queued';
      title.classList.add('muted');
      actions.innerHTML = '';
    } else {
      currentJobId = job.job_id;
      pill.dataset.status = job.status;
      pill.textContent = STATUS_LABELS[job.status] || job.status;
      title.textContent = job.status === 'error'
        ? (job.error_message || 'Something went wrong')
        : (job.title || 'Loading title…');
      title.classList.remove('muted');
      actions.innerHTML = job.status === 'error'
        ? '<button type="button" class="mini-btn" data-action="retry">Retry</button>'
        : '<button type="button" class="mini-btn" data-action="cancel">Cancel</button>';
    }
  } catch {
    // transient network hiccup, next poll will retry
  }
}

$('#now-actions').addEventListener('click', async (e) => {
  const btn = e.target.closest('.mini-btn');
  if (!btn || !currentJobId) return;
  btn.disabled = true;
  try {
    await api(`/api/jobs/${currentJobId}/${btn.dataset.action}`, { method: 'POST' });
  } catch {
    // ignore, poll will resync
  }
  pollNowPlaying();
});

// ── Device log ───────────────────────────────────────────────────────

async function pollDeviceLog() {
  try {
    const res = await api('/api/device/log');
    const data = await res.json();
    const el = $('#device-log');
    const wasAtBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 24;
    el.textContent = data.entries.length === 0
      ? 'No log lines yet — the desk unit sends these as it runs.'
      : data.entries.map((e) => `[${e.ts}] ${e.line}`).join('\n');
    if (wasAtBottom) el.scrollTop = el.scrollHeight;
  } catch {
    // next poll retries
  }
}

$('#clear-log').addEventListener('click', async (e) => {
  e.preventDefault();
  try {
    await api('/api/device/log/clear', { method: 'POST' });
  } catch {
    // ignore, next poll resyncs
  }
  pollDeviceLog();
});

// ── Todos ────────────────────────────────────────────────────────────

function renderTodos(todos) {
  const list = $('#todo-list');
  list.innerHTML = '';
  for (const t of todos) {
    const li = document.createElement('li');
    li.className = 'todo-item' + (t.done ? ' done' : '');
    li.innerHTML = `
      <input type="checkbox" ${t.done ? 'checked' : ''} data-id="${t.id}">
      <span>${escapeHtml(t.text)}</span>
      <button class="todo-del" data-id="${t.id}" aria-label="Delete">&times;</button>
    `;
    list.appendChild(li);
  }
  updateDeskStatus(todos);
}

async function loadTodos() {
  const res = await api('/api/todos');
  renderTodos(await res.json());
}

$('#todo-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const input = $('#todo-input');
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  await api('/api/todos', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  });
  loadTodos();
});

$('#todo-list').addEventListener('change', async (e) => {
  if (e.target.type !== 'checkbox') return;
  await api(`/api/todos/${e.target.dataset.id}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ done: e.target.checked }),
  });
  loadTodos();
});

$('#todo-list').addEventListener('click', async (e) => {
  const btn = e.target.closest('.todo-del');
  if (!btn) return;
  await api(`/api/todos/${btn.dataset.id}`, { method: 'DELETE' });
  loadTodos();
});

$('#clear-done').addEventListener('click', async (e) => {
  e.preventDefault();
  await api('/api/todos/clear_completed', { method: 'POST' });
  loadTodos();
});

// ── Boot ─────────────────────────────────────────────────────────────

let booted = false;
function boot() {
  if (booted) return;
  booted = true;
  tickClock();
  setInterval(tickClock, 1000);
  loadSettings();
  loadTodos();
  pollNowPlaying();
  setInterval(pollNowPlaying, 2500);
  pollDeviceLog();
  setInterval(pollDeviceLog, 3000);
}

checkAuth();

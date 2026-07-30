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
  const hh = String(now.getHours()).padStart(2, '0');
  const mm = String(now.getMinutes()).padStart(2, '0');
  const ss = String(now.getSeconds()).padStart(2, '0');
  $('#clock').textContent = `${hh}:${mm}:${ss}`;
  $('#date').textContent = now.toLocaleDateString(undefined, {
    weekday: 'short', month: 'short', day: 'numeric',
  });
}

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

async function pollNowPlaying() {
  try {
    const res = await api('/api/jobs?active=1');
    const jobs = await res.json();
    const pill = $('#now-status');
    const title = $('#now-title');
    if (jobs.length === 0) {
      pill.dataset.status = 'idle';
      pill.textContent = 'Idle';
      title.textContent = 'Nothing queued';
      title.classList.add('muted');
    } else {
      const job = jobs[0];
      pill.dataset.status = job.status;
      pill.textContent = STATUS_LABELS[job.status] || job.status;
      title.textContent = job.status === 'error'
        ? (job.error_message || 'Something went wrong')
        : (job.title || 'Loading title…');
      title.classList.remove('muted');
    }
  } catch {
    // transient network hiccup, next poll will retry
  }
}

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

// ── Boot ─────────────────────────────────────────────────────────────

let booted = false;
function boot() {
  if (booted) return;
  booted = true;
  tickClock();
  setInterval(tickClock, 1000);
  loadTodos();
  pollNowPlaying();
  setInterval(pollNowPlaying, 2500);
}

checkAuth();

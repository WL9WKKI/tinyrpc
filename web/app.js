// ---- 视图切换 ----
document.querySelectorAll('.nav-item').forEach(a => {
  a.addEventListener('click', e => {
    e.preventDefault();
    document.querySelectorAll('.nav-item').forEach(x => x.classList.remove('active'));
    a.classList.add('active');
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    document.getElementById('view-' + a.dataset.view).classList.add('active');
    if (a.dataset.view === 'tasks') loadTasks();
    if (a.dataset.view === 'metrics') loadMetrics();
    if (a.dataset.view === 'config') loadConfig();
  });
});

// ---- 诊断 ----
document.getElementById('diagnose-form').addEventListener('submit', async e => {
  e.preventDefault();
  const body = {
    service_name: document.getElementById('diag-service').value,
    symptom: document.getElementById('diag-symptom').value,
    severity: Number(document.getElementById('diag-severity').value),
    max_steps: Number(document.getElementById('diag-max-steps').value),
  };
  const resp = await fetch('/api/tasks', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
  });
  const data = await resp.json();
  if (!resp.ok) { alert('提交失败: ' + JSON.stringify(data)); return; }
  currentTaskId = data.task_id;
  resetResult();
  pollTask(currentTaskId);
});

let currentTaskId = null;
let pollTimer = null;

function resetResult() {
  document.getElementById('diagnose-result').classList.remove('hidden');
  document.getElementById('diag-answer').textContent = '';
  document.getElementById('diag-evidence').innerHTML = '';
  document.getElementById('diag-trace').innerHTML = '';
  document.getElementById('diag-confidence-wrap').classList.add('hidden');
  document.getElementById('diag-cancel').classList.remove('hidden');
}

async function pollTask(taskId) {
  if (pollTimer) clearTimeout(pollTimer);
  const resp = await fetch('/api/tasks/' + taskId);
  if (!resp.ok) return;
  const task = await resp.json();
  renderTask(task);
  if (['SUCCEEDED', 'FAILED', 'TIMEOUT', 'CANCELLED'].includes(task.status)) {
    document.getElementById('diag-cancel').classList.add('hidden');
    const tr = await fetch('/api/tasks/' + taskId + '/trace');
    renderTrace(await tr.json());
    return;
  }
  pollTimer = setTimeout(() => pollTask(taskId), 1000);
}

function renderTask(task) {
  const badge = document.getElementById('diag-status');
  badge.textContent = task.status;
  badge.className = 'badge ' + task.status;
  document.getElementById('diag-step').textContent =
    '当前步 ' + (task.current_step || 0) + '/' + (task.max_steps || 0);

  if (task.status === 'SUCCEEDED') {
    const wrap = document.getElementById('diag-confidence-wrap');
    wrap.classList.remove('hidden');
    document.getElementById('diag-confidence').textContent = (task.confidence || 0).toFixed(3);
    const lv = document.getElementById('diag-level');
    lv.textContent = task.level || '';
    lv.className = 'badge ' + (task.level || '');
    document.getElementById('diag-conf-fill').style.width = ((task.confidence || 0) * 100) + '%';
    document.getElementById('diag-answer').textContent = task.answer || '';
    const ev = document.getElementById('diag-evidence');
    ev.innerHTML = (task.evidence || []).map(x => '<div class="ev-item">' + escapeHtml(x) + '</div>').join('');
  } else if (task.error_message) {
    document.getElementById('diag-answer').textContent = task.error_message;
  }
}

function renderTrace(steps) {
  const tl = document.getElementById('diag-trace');
  tl.innerHTML = (steps || []).map(s =>
    '<div class="tl-item">' +
      '<div class="tl-action">step ' + s.step_no + ' · ' + escapeHtml(s.action) + '</div>' +
      '<div class="tl-obs">' + escapeHtml(truncate(s.observation, 200)) + '</div>' +
    '</div>'
  ).join('');
}

document.getElementById('diag-cancel').addEventListener('click', async () => {
  if (currentTaskId) await fetch('/api/tasks/' + currentTaskId + '/cancel', { method: 'POST' });
});

// ---- 任务列表 ----
async function loadTasks() {
  const resp = await fetch('/api/tasks');
  const data = await resp.json();
  const body = document.getElementById('tasks-body');
  body.innerHTML = (data.tasks || []).map(t =>
    '<tr data-id="' + t.task_id + '">' +
      '<td>' + t.task_id + '</td>' +
      '<td>' + escapeHtml(t.service_name) + '</td>' +
      '<td>' + escapeHtml(truncate(t.symptom, 30)) + '</td>' +
      '<td><span class="badge ' + t.status + '">' + t.status + '</span></td>' +
      '<td>' + (t.confidence ? t.confidence.toFixed(3) : '-') + '</td>' +
      '<td>' + t.current_step + '</td>' +
    '</tr>'
  ).join('');
  body.querySelectorAll('tr').forEach(tr => {
    tr.addEventListener('click', () => {
      document.querySelectorAll('.nav-item').forEach(x => x.classList.remove('active'));
      document.querySelector('[data-view="diagnose"]').classList.add('active');
      document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
      document.getElementById('view-diagnose').classList.add('active');
      currentTaskId = tr.dataset.id;
      resetResult();
      pollTask(currentTaskId);
    });
  });
}

// ---- 指标 ----
async function loadMetrics() {
  const resp = await fetch('/api/metrics');
  const m = await resp.json();
  const container = document.getElementById('metrics-cards');
  container.innerHTML = Object.entries(m).map(([name, v]) => {
    const max = Math.max(v.p50_ms, v.p95_ms, v.p99_ms, 1);
    const bar = (label, val) =>
      '<div class="bar-row"><span>' + label + '</span>' +
      '<div class="bar"><i style="width:' + (val / max * 100) + '%"></i></div>' +
      '<span>' + val.toFixed(1) + 'ms</span></div>';
    return '<div class="card metric-card">' +
      '<div class="name">' + name + ' <span class="muted">(count ' + v.count + ')</span></div>' +
      bar('P50', v.p50_ms) + bar('P95', v.p95_ms) + bar('P99', v.p99_ms) +
      '<div class="muted">mean ' + v.mean_ms.toFixed(1) + 'ms</div>' +
    '</div>';
  }).join('');
}

// ---- 配置 ----
async function loadConfig() {
  const resp = await fetch('/api/config');
  const config = await resp.json();
  const form = document.getElementById('config-form');
  form.innerHTML = '';
  renderConfigForm(config, form);
}

function renderConfigForm(obj, container, path = '') {
  for (const [key, val] of Object.entries(obj)) {
    const fullKey = path ? path + '.' + key : key;
    const field = document.createElement('div');
    field.className = 'field';
    const label = document.createElement('label');
    label.textContent = fullKey;
    field.appendChild(label);

    if (val && typeof val === 'object' && !Array.isArray(val)) {
      const sub = document.createElement('div');
      sub.className = 'sub';
      renderConfigForm(val, sub, fullKey);
      field.appendChild(sub);
    } else if (typeof val === 'boolean') {
      const input = document.createElement('input');
      input.type = 'checkbox'; input.checked = val;
      input.dataset.path = fullKey; input.dataset.type = 'bool';
      field.appendChild(input);
    } else if (typeof val === 'number') {
      const input = document.createElement('input');
      input.type = 'number'; input.value = val;
      input.dataset.path = fullKey; input.dataset.type = 'number';
      field.appendChild(input);
    } else {
      const input = document.createElement('input');
      input.type = 'text'; input.value = val;
      input.dataset.path = fullKey; input.dataset.type = 'string';
      field.appendChild(input);
    }
    container.appendChild(field);
  }
}

function setPath(obj, path, val) {
  const parts = path.split('.');
  let cur = obj;
  for (let i = 0; i < parts.length - 1; i++) {
    if (!cur[parts[i]] || typeof cur[parts[i]] !== 'object') cur[parts[i]] = {};
    cur = cur[parts[i]];
  }
  cur[parts[parts.length - 1]] = val;
}

document.getElementById('config-save').addEventListener('click', async () => {
  const result = {};
  document.querySelectorAll('#config-form input[data-path]').forEach(input => {
    let val;
    if (input.dataset.type === 'number') val = Number(input.value);
    else if (input.dataset.type === 'bool') val = input.checked;
    else val = input.value;
    setPath(result, input.dataset.path, val);
  });
  const resp = await fetch('/api/config', {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(result)
  });
  const data = await resp.json();
  const el = document.getElementById('config-result');
  if (data.ok) {
    el.textContent = '已保存。热加载: [' + (data.hot_reloaded || []).join(', ') +
      '] 需重启: [' + (data.restart_required || []).join(', ') + ']';
  } else {
    el.textContent = '保存失败: ' + JSON.stringify(data);
  }
});

// ---- 工具 ----
function escapeHtml(s) {
  return String(s || '').replace(/[&<>"]/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}
function truncate(s, n) {
  s = String(s || '');
  return s.length > n ? s.slice(0, n) + '…' : s;
}

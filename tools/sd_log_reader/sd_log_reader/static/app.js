const ui = {
  sourcePath: document.querySelector('#source-path'),
  loadButton: document.querySelector('#load-button'),
  browseButton: document.querySelector('#browse-button'),
  driveList: document.querySelector('#drive-list'),
  emptyState: document.querySelector('#empty-state'),
  workspace: document.querySelector('#workspace'),
  loading: document.querySelector('#loading'),
  toast: document.querySelector('#toast'),
  sourceState: document.querySelector('#source-state'),
  browserDialog: document.querySelector('#browser-dialog'),
  browserPath: document.querySelector('#browser-path'),
  browserList: document.querySelector('#browser-list'),
  browserParent: document.querySelector('#browser-parent'),
  browserGo: document.querySelector('#browser-go'),
  browserSelect: document.querySelector('#browser-select'),
  browserHint: document.querySelector('#browser-hint'),
  recordDialog: document.querySelector('#record-dialog'),
  recordTitle: document.querySelector('#record-title'),
  recordDetail: document.querySelector('#record-detail'),
};

const state = { snapshot: null, offset: 0, pageSize: 100, records: [], browser: null };

async function api(url, options = {}) {
  const response = await fetch(url, options);
  if (!response.ok) {
    let message = `${response.status} ${response.statusText}`;
    try { message = (await response.json()).detail || message; } catch (_) { /* no-op */ }
    throw new Error(message);
  }
  return response.json();
}

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>'"]/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char]));
}
function fmtNumber(value, digits = 0) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return '—';
  return Number(value).toLocaleString('zh-CN', { maximumFractionDigits: digits, minimumFractionDigits: digits });
}
function fmtBytes(value) {
  if (value === null || value === undefined) return '—';
  const units = ['B', 'KiB', 'MiB', 'GiB']; let size = Number(value); let index = 0;
  while (size >= 1024 && index < units.length - 1) { size /= 1024; index += 1; }
  return `${size.toFixed(index === 0 ? 0 : size >= 100 ? 0 : 1)} ${units[index]}`;
}
function showToast(message) {
  ui.toast.textContent = message; ui.toast.classList.remove('hidden');
  clearTimeout(showToast.timer); showToast.timer = setTimeout(() => ui.toast.classList.add('hidden'), 6000);
}
function setLoading(active) { ui.loading.classList.toggle('hidden', !active); }

async function loadDrives() {
  try {
    const data = await api('/api/drives');
    if (!data.drives.length) { ui.driveList.innerHTML = '<span class="muted">未发现可用磁盘</span>'; return; }
    ui.driveList.innerHTML = data.drives.map(drive => `
      <button class="drive-chip ${drive.has_log_directory ? 'ready-log' : ''}" data-path="${escapeHtml(drive.path)}" type="button">
        <span class="drive-icon">${drive.type === 'removable' ? '◆' : '▣'}</span>
        <span class="drive-copy"><strong>${escapeHtml(drive.path)} ${escapeHtml(drive.label)}</strong><small>${escapeHtml(drive.filesystem || drive.type)}${drive.has_log_directory ? ' · 发现 LOG' : ''}</small></span>
      </button>`).join('');
    ui.driveList.querySelectorAll('.drive-chip').forEach(button => button.addEventListener('click', () => {
      ui.sourcePath.value = button.dataset.path; browsePath(button.dataset.path);
    }));
  } catch (error) { ui.driveList.innerHTML = `<span class="muted">${escapeHtml(error.message)}</span>`; }
}

async function loadSource(pathValue) {
  const path = (pathValue || ui.sourcePath.value).trim();
  if (!path) { showToast('请先输入或选择 SD 卡路径'); return; }
  ui.sourcePath.value = path; setLoading(true);
  try {
    const snapshot = await api('/api/load', {
      method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({path}),
    });
    state.snapshot = snapshot; state.offset = 0; renderSnapshot(snapshot); await loadRecords();
  } catch (error) { showToast(error.message); }
  finally { setLoading(false); }
}

function renderSnapshot(snapshot) {
  const s = snapshot.summary;
  ui.emptyState.classList.add('hidden'); ui.workspace.classList.remove('hidden');
  ui.sourceState.textContent = s.problematic_file_count ? `已读取 · ${s.problematic_file_count} 个文件有问题` : '已读取 · 校验通过';
  ui.sourceState.className = `state-pill ${s.problematic_file_count ? 'warn' : 'good'}`;
  document.querySelector('#total-records').textContent = fmtNumber(s.total_records);
  document.querySelector('#record-subtitle').textContent = `共 ${fmtBytes(s.total_bytes)} 原始文件`;
  document.querySelector('#total-files').textContent = fmtNumber(s.file_count);
  document.querySelector('#file-subtitle').textContent = `${s.clean_file_count} 个完全正常 · ${s.usable_file_count} 个可读取`;
  document.querySelector('#missing-sequences').textContent = fmtNumber(s.missed_sequence_count);
  document.querySelector('#gap-subtitle').textContent = `${s.sequence_gap_count} 处跳号 · ${s.out_of_order_count} 处倒序/重复`;
  document.querySelector('#total-issues').textContent = fmtNumber(s.issue_count);
  document.querySelector('#issue-subtitle').textContent = `${s.problematic_file_count} 个受影响文件`;
  renderFiles(snapshot.files); renderMetrics(s.stats); renderCharts(snapshot.preview, s.total_records);
}

function renderFiles(files) {
  const body = document.querySelector('#file-table-body');
  body.innerHTML = files.map(file => {
    const level = file.clean ? 'ok' : file.usable ? 'warn' : 'error';
    const label = file.clean ? '正常' : file.usable ? '部分有效' : '不可解析';
    const issues = file.issues.length ? file.issues.map(issue => `${issue.code}: ${issue.message}`).join('<br>') : '—';
    return `<tr>
      <td><strong>${escapeHtml(file.name)}</strong><br><span class="muted">index ${file.header?.file_index ?? '—'}</span></td>
      <td><span class="status-badge ${level}">${label}</span></td>
      <td class="number">${fmtBytes(file.size_bytes)}</td>
      <td class="number">${fmtNumber(file.valid_records)} / ${fmtNumber(file.possible_records)}</td>
      <td class="number">${fmtNumber(file.first_sequence)} → ${fmtNumber(file.last_sequence)}</td>
      <td class="number">${fmtNumber(file.first_tick_ms)} → ${fmtNumber(file.last_tick_ms)}</td>
      <td class="issue-cell">${issues}</td></tr>`;
  }).join('');
}

function renderMetrics(stats) {
  const definitions = [
    ['pressure', '压力', '工程值'], ['ec', '电导率', '寄存器值 ÷ 100'], ['temperature', '温度', '°C'], ['tds', 'TDS', 'ppm'], ['salinity', '盐度', 'ppm'],
  ];
  document.querySelector('#metric-grid').innerHTML = definitions.map(([key, label, unit]) => {
    const metric = stats.metrics[key];
    return `<article class="metric-card"><h3>${label}</h3><strong>${fmtNumber(metric.avg, key === 'tds' || key === 'salinity' ? 1 : 2)}</strong><p>最小 ${fmtNumber(metric.min, 2)} · 最大 ${fmtNumber(metric.max, 2)}<br>${unit} · ${fmtNumber(metric.count)} 个有效值</p></article>`;
  }).join('');
}

function drawChart(canvas, points, series) {
  const rect = canvas.getBoundingClientRect(); const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * ratio)); canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
  const width = rect.width, height = rect.height, pad = {left: 51, right: 16, top: 12, bottom: 27};
  ctx.clearRect(0, 0, width, height); ctx.font = '10px Segoe UI';
  const values = series.flatMap(item => points.map(point => item.value(point)).filter(Number.isFinite));
  if (!values.length) { ctx.fillStyle = '#7893a4'; ctx.fillText('无有效数据', pad.left, height / 2); return; }
  let min = Math.min(...values), max = Math.max(...values); if (min === max) { min -= 1; max += 1; }
  const margin = (max - min) * .07; min -= margin; max += margin;
  ctx.strokeStyle = 'rgba(143,183,207,.11)'; ctx.fillStyle = '#7893a4'; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) {
    const y = pad.top + (height - pad.top - pad.bottom) * i / 4;
    ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(width - pad.right, y); ctx.stroke();
    const label = max - (max - min) * i / 4; ctx.fillText(fmtNumber(label, 2), 4, y + 3);
  }
  const xAt = i => pad.left + (width - pad.left - pad.right) * (points.length <= 1 ? 0 : i / (points.length - 1));
  const yAt = value => pad.top + (height - pad.top - pad.bottom) * (max - value) / (max - min);
  series.forEach(item => {
    ctx.strokeStyle = item.color; ctx.lineWidth = 1.5; ctx.beginPath(); let started = false;
    points.forEach((point, index) => { const value = item.value(point); if (!Number.isFinite(value)) { started = false; return; } const x = xAt(index), y = yAt(value); if (!started) { ctx.moveTo(x, y); started = true; } else ctx.lineTo(x, y); }); ctx.stroke();
  });
  ctx.fillStyle = '#7893a4';
  const first = points[0]?.global_index ?? 0, last = points[points.length - 1]?.global_index ?? 0;
  ctx.fillText(fmtNumber(first), pad.left, height - 7); ctx.textAlign = 'right'; ctx.fillText(fmtNumber(last), width - pad.right, height - 7); ctx.textAlign = 'left';
  let legendX = pad.left + 5; series.forEach(item => { ctx.fillStyle = item.color; ctx.fillRect(legendX, pad.top + 3, 12, 2); ctx.fillStyle = '#9fb8c8'; ctx.fillText(item.label, legendX + 17, pad.top + 7); legendX += ctx.measureText(item.label).width + 45; });
}

function renderCharts(points, total) {
  document.querySelector('#preview-note').textContent = `${fmtNumber(points.length)} 个下采样点 / ${fmtNumber(total)} 条有效记录`;
  const charts = [
    ['pressure-chart', [{label:'压力', color:'#4da9ff', value:p => p.pressure}]],
    ['temperature-chart', [{label:'温度', color:'#ffba55', value:p => p.temperature}]],
    ['ec-chart', [{label:'EC', color:'#27dfd3', value:p => p.ec}]],
    ['ppm-chart', [{label:'TDS', color:'#9f8cff', value:p => p.tds_ppm}, {label:'盐度', color:'#ff6978', value:p => p.salinity_ppm}]],
  ];
  requestAnimationFrame(() => charts.forEach(([id, series]) => drawChart(document.getElementById(id), points, series)));
}

function statusBadge(online, name) {
  const level = !online || !['IDLE','OK'].includes(name) ? 'error' : name === 'OK' ? 'ok' : 'idle';
  return `<span class="status-badge ${level}">${online ? name : 'OFFLINE'}</span>`;
}

async function loadRecords() {
  if (!state.snapshot) return;
  try {
    const data = await api(`/api/records?offset=${state.offset}&limit=${state.pageSize}`); state.records = data.records;
    document.querySelector('#records-body').innerHTML = data.records.map((r, index) => `<tr data-index="${index}">
      <td class="number">${fmtNumber(r.global_index)}</td><td><strong>${escapeHtml(r.source_file)}</strong><br><span class="muted">record ${fmtNumber(r.record_index)}</span></td>
      <td class="number">${fmtNumber(r.sequence)}</td><td class="number">${fmtNumber(r.tick_ms)}</td><td class="number">${fmtNumber(r.pressure, 4)}</td><td class="number">${fmtNumber(r.ec, 2)}</td><td class="number">${fmtNumber(r.temperature, 1)}</td><td class="number">${fmtNumber(r.tds_ppm)}</td><td class="number">${fmtNumber(r.salinity_ppm)}</td>
      <td>${statusBadge(r.pressure_online, r.pressure_status_name)}</td><td>${statusBadge(r.xda_online, r.xda_status_name)}</td></tr>`).join('');
    document.querySelectorAll('#records-body tr').forEach(row => row.addEventListener('click', () => showRecord(state.records[Number(row.dataset.index)])));
    const start = data.total ? data.offset + 1 : 0, end = Math.min(data.offset + data.limit, data.total);
    document.querySelector('#page-label').textContent = `${fmtNumber(start)}–${fmtNumber(end)} / ${fmtNumber(data.total)}`;
    document.querySelector('#prev-page').disabled = state.offset === 0; document.querySelector('#next-page').disabled = end >= data.total;
  } catch (error) { showToast(error.message); }
}

function detailGroup(title, fields, record) {
  return `<section class="detail-group"><h3>${title}</h3>${fields.map(([key, label, transform]) => `<div class="detail-row"><span>${label}</span><code>${escapeHtml(transform ? transform(record[key], record) : record[key] ?? '—')}</code></div>`).join('')}</section>`;
}
function showRecord(record) {
  ui.recordTitle.textContent = `${record.source_file} · record ${record.record_index}`;
  const bool = value => value ? 'true' : 'false'; const hex = value => `0x${Number(value).toString(16).toUpperCase().padStart(8,'0')}`;
  ui.recordDetail.innerHTML = [
    detailGroup('定位与时间', [['global_index','全局索引'],['file_offset','文件偏移'],['sequence','Sequence'],['tick_ms','Tick ms']], record),
    detailGroup('压力传感器', [['pressure','工程值'],['pressure_value','Float 值'],['pressure_raw','Raw 值'],['pressure_unit_code','单位代码'],['pressure_decimal_point','小数位'],['pressure_read_mode_name','读取模式'],['pressure_float_valid','Float 有效',bool],['pressure_online','在线',bool],['pressure_status_name','状态'],['pressure_sample_tick','样本 Tick'],['pressure_sample_age_ms','样本年龄 ms'],['pressure_exception_code','异常码']], record),
    detailGroup('XDA 四合一', [['ec','EC'],['temperature','温度 °C'],['tds_ppm','TDS ppm'],['salinity_ppm','盐度 ppm'],['xda_online','在线',bool],['xda_status_name','状态'],['xda_sample_tick','样本 Tick'],['xda_sample_age_ms','样本年龄 ms'],['xda_exception_code','异常码']], record),
    detailGroup('CRC', [['crc32_stored','存储值',hex],['crc32_calculated','计算值',hex]], record),
  ].join(''); ui.recordDialog.showModal();
}

async function browsePath(pathValue) {
  const path = (pathValue || ui.browserPath.value || ui.sourcePath.value).trim(); if (!path) return;
  try {
    const data = await api(`/api/browse?path=${encodeURIComponent(path)}`); state.browser = data; ui.browserPath.value = data.path;
    ui.browserParent.disabled = !data.parent; ui.browserHint.textContent = data.has_log_directory ? '当前目录包含 LOG 子目录' : `${data.entries.length} 个可选项目`;
    ui.browserList.innerHTML = data.entries.length ? data.entries.map(entry => `<button class="browser-entry" type="button" data-path="${escapeHtml(entry.path)}" data-dir="${entry.is_dir}"><span>${entry.is_dir ? '▸' : '▤'}</span><strong>${escapeHtml(entry.name)}</strong><small>${entry.is_dir ? '目录' : fmtBytes(entry.size_bytes)}</small></button>`).join('') : '<div class="empty-state"><p>没有子目录或 LOGxxxxx.BIN 文件</p></div>';
    ui.browserList.querySelectorAll('.browser-entry').forEach(button => button.addEventListener('click', () => {
      if (button.dataset.dir === 'true') browsePath(button.dataset.path); else { ui.browserDialog.close(); loadSource(button.dataset.path); }
    }));
    if (!ui.browserDialog.open) ui.browserDialog.showModal();
  } catch (error) { showToast(error.message); }
}

ui.loadButton.addEventListener('click', () => loadSource());
ui.sourcePath.addEventListener('keydown', event => { if (event.key === 'Enter') loadSource(); });
ui.browseButton.addEventListener('click', () => browsePath(ui.sourcePath.value || 'C:\\'));
ui.browserParent.addEventListener('click', () => state.browser?.parent && browsePath(state.browser.parent));
ui.browserGo.addEventListener('click', () => browsePath()); ui.browserPath.addEventListener('keydown', event => { if (event.key === 'Enter') browsePath(); });
ui.browserSelect.addEventListener('click', () => { if (!state.browser) return; ui.browserDialog.close(); loadSource(state.browser.suggested_source); });
document.querySelector('#prev-page').addEventListener('click', () => { state.offset = Math.max(0, state.offset - state.pageSize); loadRecords(); });
document.querySelector('#next-page').addEventListener('click', () => { state.offset += state.pageSize; loadRecords(); });
window.addEventListener('resize', () => { if (state.snapshot) renderCharts(state.snapshot.preview, state.snapshot.summary.total_records); });

loadDrives();
const startupSource = new URLSearchParams(window.location.search).get('source');
if (startupSource) { ui.sourcePath.value = startupSource; loadSource(startupSource); }

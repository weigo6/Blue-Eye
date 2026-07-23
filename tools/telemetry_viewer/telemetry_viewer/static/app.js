const state = {
  status: null,
  latestFrame: null,
  records: [],
  frameHistory: [],
  filter: 'all',
  websocket: null,
  pingTimer: null,
  toastTimer: null,
}

const $ = id => document.getElementById(id)

const elements = {
  wsBadge: $('wsBadge'),
  deviceBadge: $('deviceBadge'),
  connectionDetail: $('connectionDetail'),
  portSelect: $('portSelect'),
  baudRateSelect: $('baudRateSelect'),
  refreshPortsButton: $('refreshPortsButton'),
  connectButton: $('connectButton'),
  disconnectButton: $('disconnectButton'),
  demoButton: $('demoButton'),
  clearButton: $('clearButton'),
  rxBytes: $('rxBytes'),
  rawChunkCount: $('rawChunkCount'),
  validFrames: $('validFrames'),
  frameRatio: $('frameRatio'),
  deviceInterval: $('deviceInterval'),
  noiseBytes: $('noiseBytes'),
  noiseRecords: $('noiseRecords'),
  pressureState: $('pressureState'),
  pressureValue: $('pressureValue'),
  pressureUnit: $('pressureUnit'),
  pressureMode: $('pressureMode'),
  pressureRaw: $('pressureRaw'),
  pressureDecimal: $('pressureDecimal'),
  pressureFloatValid: $('pressureFloatValid'),
  xdaState: $('xdaState'),
  ecValue: $('ecValue'),
  temperatureValue: $('temperatureValue'),
  tdsValue: $('tdsValue'),
  salinityValue: $('salinityValue'),
  checksumState: $('checksumState'),
  deviceTick: $('deviceTick'),
  hostReceivedAt: $('hostReceivedAt'),
  frameLength: $('frameLength'),
  checksumValue: $('checksumValue'),
  frameError: $('frameError'),
  frameTableBody: $('frameTableBody'),
  rawRecords: $('rawRecords'),
  trendCanvas: $('trendCanvas'),
  toast: $('toast'),
}

function setBadge(element, text, variant = 'neutral') {
  element.className = `status-pill ${variant}`
  element.innerHTML = '<i></i>'
  element.append(document.createTextNode(text))
}

function setSensorState(element, text, variant = 'neutral') {
  element.className = `sensor-state ${variant}`
  element.textContent = text
}

function formatBytes(value) {
  const bytes = Number(value || 0)
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 ** 2) return `${(bytes / 1024).toFixed(1)} KiB`
  return `${(bytes / 1024 ** 2).toFixed(2)} MiB`
}

function formatHostTime(value, withDate = false) {
  if (!value) return '—'
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return value
  const options = {
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    fractionalSecondDigits: 3,
  }
  if (withDate) Object.assign(options, { year: 'numeric', month: '2-digit', day: '2-digit' })
  return new Intl.DateTimeFormat('zh-CN', options).format(date)
}

function safeNumber(value, digits = 2) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return '—'
  return Number(value).toFixed(digits)
}

function showToast(message, isError = false) {
  clearTimeout(state.toastTimer)
  elements.toast.textContent = message
  elements.toast.className = `toast${isError ? ' error' : ''}`
  state.toastTimer = setTimeout(() => elements.toast.classList.add('hidden'), 4200)
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { 'Content-Type': 'application/json', ...(options.headers || {}) },
    ...options,
  })
  if (!response.ok) {
    let detail = `${response.status} ${response.statusText}`
    try {
      const body = await response.json()
      detail = body.detail || detail
    }
    catch {}
    throw new Error(detail)
  }
  return response.json()
}

async function loadPorts() {
  elements.refreshPortsButton.disabled = true
  try {
    const result = await api('/api/ports')
    const previous = elements.portSelect.value
    elements.portSelect.replaceChildren()
    if (!result.ports.length) {
      const option = new Option('未发现串口设备', '')
      elements.portSelect.add(option)
    }
    else {
      for (const port of result.ports) {
        const detail = port.description && port.description !== 'n/a'
          ? `${port.device} — ${port.description}`
          : port.device
        elements.portSelect.add(new Option(detail, port.device))
      }
      if (result.ports.some(port => port.device === previous)) elements.portSelect.value = previous
    }
  }
  catch (error) {
    showToast(`串口扫描失败：${error.message}`, true)
  }
  finally {
    elements.refreshPortsButton.disabled = false
  }
}

async function connectSerial() {
  const port = elements.portSelect.value
  if (!port) {
    showToast('请先选择串口设备', true)
    return
  }
  elements.connectButton.disabled = true
  try {
    const snapshot = await api('/api/connect', {
      method: 'POST',
      body: JSON.stringify({
        port,
        baud_rate: Number(elements.baudRateSelect.value),
        data_bits: 8,
        parity: 'N',
        stop_bits: 1,
      }),
    })
    applySnapshot(snapshot)
    showToast(`已连接 ${port}`)
  }
  catch (error) {
    showToast(error.message, true)
  }
  finally {
    elements.connectButton.disabled = false
  }
}

async function startDemo() {
  elements.demoButton.disabled = true
  try {
    const snapshot = await api('/api/demo', {
      method: 'POST',
      body: JSON.stringify({ period_ms: 1000, inject_noise: true }),
    })
    applySnapshot(snapshot)
    showToast('演示模式已启动：模拟分块并定期注入 Modbus 噪声')
  }
  catch (error) {
    showToast(error.message, true)
  }
  finally {
    elements.demoButton.disabled = false
  }
}

async function disconnect() {
  elements.disconnectButton.disabled = true
  try {
    const snapshot = await api('/api/disconnect', { method: 'POST', body: '{}' })
    applySnapshot(snapshot)
    showToast('连接已断开')
  }
  catch (error) {
    showToast(error.message, true)
  }
}

async function clearRecords() {
  try {
    const snapshot = await api('/api/clear', { method: 'POST', body: '{}' })
    applySnapshot(snapshot)
    showToast('记录和统计已清空')
  }
  catch (error) {
    showToast(error.message, true)
  }
}

function renderStatus(status) {
  if (!status) return
  state.status = status
  const connected = status.connected
  elements.connectButton.disabled = connected
  elements.disconnectButton.disabled = !connected
  elements.portSelect.disabled = connected
  elements.baudRateSelect.disabled = connected

  if (status.mode === 'serial') {
    setBadge(elements.deviceBadge, `${status.connection.port} 已连接`, 'good')
    elements.connectionDetail.textContent = `${status.connection.baud_rate} / ${status.connection.data_bits}${status.connection.parity}${status.connection.stop_bits} / 无流控`
  }
  else if (status.mode === 'demo') {
    setBadge(elements.deviceBadge, '演示数据源', 'demo')
    elements.connectionDetail.textContent = `演示周期 ${status.connection.period_ms} ms，包含分块与噪声`
  }
  else {
    setBadge(elements.deviceBadge, '未连接设备', 'neutral')
    elements.connectionDetail.textContent = '默认参数：9600 / 8N1 / 无流控'
  }

  elements.rxBytes.textContent = formatBytes(status.rx_bytes)
  elements.rawChunkCount.textContent = `${status.raw_chunk_count} 个原始块`
  elements.validFrames.textContent = status.valid_frame_count
  elements.frameRatio.textContent = status.frame_count
    ? `${status.valid_frame_count}/${status.frame_count} 帧有效`
    : '尚无帧'
  elements.deviceInterval.textContent = status.last_device_interval_ms == null
    ? '—'
    : `${status.last_device_interval_ms} ms`
  elements.noiseBytes.textContent = formatBytes(status.noise_bytes)
  elements.noiseRecords.textContent = `${status.noise_record_count} 段`
}

function renderLatestFrame(frame) {
  state.latestFrame = frame
  if (!frame) {
    setSensorState(elements.pressureState, 'NO DATA')
    setSensorState(elements.xdaState, 'NO DATA')
    setSensorState(elements.checksumState, 'WAITING')
    return
  }

  const values = frame.values || {}
  const derived = frame.derived || {}
  const pressureOnline = values.pressure_online !== 0
  const xdaOnline = values.xda_online !== 0
  setSensorState(
    elements.pressureState,
    pressureOnline ? derived.pressure_status_text : `OFFLINE / ${derived.pressure_status_text || 'UNKNOWN'}`,
    pressureOnline && values.pressure_status === 1 ? 'good' : values.pressure_status === 0 ? 'neutral' : 'bad',
  )
  setSensorState(
    elements.xdaState,
    xdaOnline ? derived.xda_status_text : `OFFLINE / ${derived.xda_status_text || 'UNKNOWN'}`,
    xdaOnline && values.xda_status === 1 ? 'good' : values.xda_status === 0 ? 'neutral' : 'bad',
  )

  const pressureDisplay = derived.pressure_value != null
    ? derived.pressure_value
    : derived.pressure_raw_scaled
  elements.pressureValue.textContent = pressureDisplay == null ? '—' : safeNumber(pressureDisplay, 3).replace(/\.000$/, '')
  elements.pressureUnit.textContent = `unit ${values.pressure_unit_code ?? '—'}`
  elements.pressureMode.textContent = derived.pressure_read_mode_text || '—'
  elements.pressureRaw.textContent = values.pressure_raw ?? '—'
  elements.pressureDecimal.textContent = values.pressure_decimal_point ?? '—'
  elements.pressureFloatValid.textContent = values.pressure_float_valid ? 'YES' : 'NO'

  elements.ecValue.textContent = safeNumber(derived.ec_value, 2)
  elements.temperatureValue.textContent = safeNumber(derived.temperature_c, 1)
  elements.tdsValue.textContent = values.tds_ppm ?? '—'
  elements.salinityValue.textContent = values.salinity_ppm ?? '—'

  setSensorState(
    elements.checksumState,
    frame.checksum_valid ? 'CHECKSUM OK' : 'CHECKSUM FAIL',
    frame.checksum_valid ? 'good' : 'bad',
  )
  elements.deviceTick.textContent = values.tick_ms == null ? '—' : `${values.tick_ms} ms`
  elements.hostReceivedAt.textContent = formatHostTime(frame.received_at, true)
  elements.frameLength.textContent = `${frame.byte_count} Bytes`
  elements.checksumValue.textContent = frame.supplied_checksum_hex
    ? `0x${frame.supplied_checksum_hex} / calc 0x${frame.calculated_checksum_hex}`
    : '—'
  if (frame.errors?.length) {
    elements.frameError.textContent = frame.errors.join('；')
    elements.frameError.classList.remove('hidden')
  }
  else {
    elements.frameError.classList.add('hidden')
  }
}

function addRecord(record) {
  state.records.push(record)
  if (state.records.length > 300) state.records.shift()
  if (record.kind === 'frame') {
    state.frameHistory.push(record.frame)
    if (state.frameHistory.length > 200) state.frameHistory.shift()
    if (record.frame.valid) renderLatestFrame(record.frame)
    renderFrameTable()
    drawTrend()
  }
  renderRawRecords()
}

function renderFrameTable() {
  const frames = state.frameHistory.slice(-200).reverse()
  elements.frameTableBody.replaceChildren()
  if (!frames.length) {
    const row = document.createElement('tr')
    row.className = 'empty-row'
    const cell = document.createElement('td')
    cell.colSpan = 10
    cell.textContent = '连接串口或启动演示模式后显示数据'
    row.append(cell)
    elements.frameTableBody.append(row)
    return
  }

  for (const frame of frames) {
    const values = frame.values || {}
    const derived = frame.derived || {}
    const row = document.createElement('tr')
    const cells = [
      formatHostTime(frame.received_at),
      values.tick_ms ?? '—',
      frame.device_interval_ms == null ? '—' : `${frame.device_interval_ms}`,
      frame.checksum_valid ? `OK ${frame.supplied_checksum_hex}` : 'FAIL',
      derived.pressure_status_text || '—',
      derived.xda_status_text || '—',
      derived.ec_value == null ? '—' : Number(derived.ec_value).toFixed(2),
      derived.temperature_c == null ? '—' : Number(derived.temperature_c).toFixed(1),
      values.tds_ppm ?? '—',
      values.salinity_ppm ?? '—',
    ]
    cells.forEach((text, index) => {
      const cell = document.createElement('td')
      cell.textContent = text
      if (index === 3) cell.className = frame.checksum_valid ? 'cell-good' : 'cell-bad'
      if (index === 4) cell.className = values.pressure_status === 1 ? 'cell-good' : values.pressure_status > 1 ? 'cell-bad' : ''
      if (index === 5) cell.className = values.xda_status === 1 ? 'cell-good' : values.xda_status > 1 ? 'cell-bad' : ''
      row.append(cell)
    })
    elements.frameTableBody.append(row)
  }
}

function recordData(record) {
  if (record.kind === 'frame') {
    return {
      time: record.frame.received_at,
      byteCount: record.frame.byte_count,
      ascii: record.frame.raw_ascii,
      hex: record.frame.raw_hex,
      reason: record.frame.errors?.join('；') || `XOR 0x${record.frame.supplied_checksum_hex}`,
    }
  }
  return {
    time: record.received_at,
    byteCount: record.byte_count,
    ascii: record.raw_ascii,
    hex: record.raw_hex,
    reason: record.reason || '',
  }
}

function renderRawRecords() {
  const selected = state.records
    .filter(record => state.filter === 'all' || record.kind === state.filter)
    .slice(-200)
    .reverse()
  elements.rawRecords.replaceChildren()
  if (!selected.length) {
    const empty = document.createElement('div')
    empty.className = 'raw-empty'
    empty.textContent = '当前过滤条件下没有记录'
    elements.rawRecords.append(empty)
    return
  }

  for (const record of selected) {
    const data = recordData(record)
    const details = document.createElement('details')
    details.className = 'raw-item'
    if (record.kind === 'noise' || (record.kind === 'frame' && !record.frame.valid)) details.open = true

    const summary = document.createElement('summary')
    const time = document.createElement('span')
    time.className = 'raw-time'
    time.textContent = formatHostTime(data.time)
    const kind = document.createElement('span')
    kind.className = `raw-kind ${record.kind}`
    kind.textContent = record.kind.toUpperCase()
    const size = document.createElement('span')
    size.className = 'raw-size'
    size.textContent = `${data.byteCount} B`
    const preview = document.createElement('span')
    preview.className = 'raw-preview'
    preview.textContent = data.ascii || '(empty)'
    summary.append(time, kind, size, preview)

    const detail = document.createElement('div')
    detail.className = 'raw-detail'
    if (data.reason) {
      const reason = document.createElement('p')
      reason.textContent = data.reason
      detail.append(reason)
    }
    for (const [labelText, value] of [['ASCII / escaped', data.ascii], ['HEX', data.hex]]) {
      const label = document.createElement('label')
      label.textContent = labelText
      const code = document.createElement('code')
      code.textContent = value || '—'
      label.append(code)
      detail.append(label)
    }
    details.append(summary, detail)
    elements.rawRecords.append(details)
  }
}

function drawTrend() {
  const canvas = elements.trendCanvas
  const rect = canvas.getBoundingClientRect()
  const ratio = window.devicePixelRatio || 1
  const width = Math.max(320, rect.width)
  const height = 310
  canvas.width = width * ratio
  canvas.height = height * ratio
  const ctx = canvas.getContext('2d')
  ctx.scale(ratio, ratio)
  ctx.clearRect(0, 0, width, height)

  const frames = state.frameHistory.filter(frame => frame.valid).slice(-60)
  if (frames.length < 2) {
    ctx.fillStyle = '#7891a2'
    ctx.font = '13px Segoe UI'
    ctx.textAlign = 'center'
    ctx.fillText('至少收到两条有效帧后绘制趋势', width / 2, height / 2)
    return
  }

  const series = [
    { label: '压力', color: '#4ad9e8', get: frame => frame.derived.pressure_value ?? frame.derived.pressure_raw_scaled },
    { label: 'EC', color: '#b494ff', get: frame => frame.derived.ec_value },
    { label: '温度 °C', color: '#ffc65b', get: frame => frame.derived.temperature_c },
  ]
  const left = 68
  const right = 16
  const top = 12
  const rowHeight = (height - top * 2) / series.length

  series.forEach((item, seriesIndex) => {
    const values = frames.map(item.get).map(value => value == null ? NaN : Number(value))
    const finite = values.filter(Number.isFinite)
    const rowTop = top + rowHeight * seriesIndex
    const plotTop = rowTop + 21
    const plotBottom = rowTop + rowHeight - 12
    ctx.strokeStyle = 'rgba(150, 197, 225, 0.10)'
    ctx.lineWidth = 1
    ctx.beginPath()
    ctx.moveTo(left, plotBottom)
    ctx.lineTo(width - right, plotBottom)
    ctx.stroke()

    ctx.fillStyle = item.color
    ctx.font = '700 11px Segoe UI'
    ctx.textAlign = 'left'
    ctx.fillText(item.label, 14, rowTop + 27)

    if (!finite.length) {
      ctx.fillStyle = '#657f90'
      ctx.font = '11px Segoe UI'
      ctx.fillText('N/A', 14, rowTop + 47)
      return
    }

    let min = Math.min(...finite)
    let max = Math.max(...finite)
    if (min === max) { min -= 1; max += 1 }
    const padding = (max - min) * 0.12
    min -= padding
    max += padding
    ctx.fillStyle = '#6f899a'
    ctx.font = '10px ui-monospace'
    ctx.fillText(`${finite.at(-1).toFixed(2)}`, 14, rowTop + 46)

    ctx.strokeStyle = item.color
    ctx.lineWidth = 1.8
    ctx.shadowColor = item.color
    ctx.shadowBlur = 6
    ctx.beginPath()
    let drawing = false
    values.forEach((value, index) => {
      if (!Number.isFinite(value)) { drawing = false; return }
      const x = left + (index / Math.max(1, values.length - 1)) * (width - left - right)
      const y = plotBottom - ((value - min) / (max - min)) * (plotBottom - plotTop)
      if (!drawing) { ctx.moveTo(x, y); drawing = true }
      else ctx.lineTo(x, y)
    })
    ctx.stroke()
    ctx.shadowBlur = 0
  })
}

function applySnapshot(snapshot) {
  state.records = snapshot.records || []
  state.frameHistory = state.records
    .filter(record => record.kind === 'frame')
    .map(record => record.frame)
  renderStatus(snapshot.status)
  renderLatestFrame(snapshot.latest_frame)
  renderFrameTable()
  renderRawRecords()
  drawTrend()
}

function handleSocketMessage(message) {
  if (message.type === 'snapshot') applySnapshot(message.snapshot)
  else if (message.type === 'status') renderStatus(message.status)
  else if (message.type === 'record') addRecord(message.record)
  else if (message.type === 'cleared') applySnapshot(message.snapshot)
  else if (message.type === 'error') showToast(message.message, true)
}

function connectWebSocket() {
  clearInterval(state.pingTimer)
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws'
  const socket = new WebSocket(`${scheme}://${location.host}/ws`)
  state.websocket = socket
  setBadge(elements.wsBadge, '界面连接中', 'neutral')
  socket.addEventListener('open', () => {
    setBadge(elements.wsBadge, '实时通道在线', 'good')
    state.pingTimer = setInterval(() => {
      if (socket.readyState === WebSocket.OPEN) socket.send('ping')
    }, 25000)
  })
  socket.addEventListener('message', event => {
    try { handleSocketMessage(JSON.parse(event.data)) }
    catch (error) { console.error('Invalid websocket message', error) }
  })
  socket.addEventListener('close', () => {
    setBadge(elements.wsBadge, '实时通道重连中', 'warn')
    clearInterval(state.pingTimer)
    setTimeout(connectWebSocket, 1500)
  })
  socket.addEventListener('error', () => socket.close())
}

elements.refreshPortsButton.addEventListener('click', loadPorts)
elements.connectButton.addEventListener('click', connectSerial)
elements.disconnectButton.addEventListener('click', disconnect)
elements.demoButton.addEventListener('click', startDemo)
elements.clearButton.addEventListener('click', clearRecords)
document.querySelectorAll('.filter-button').forEach(button => {
  button.addEventListener('click', () => {
    document.querySelectorAll('.filter-button').forEach(item => item.classList.remove('active'))
    button.classList.add('active')
    state.filter = button.dataset.filter
    renderRawRecords()
  })
})
window.addEventListener('resize', drawTrend)

Promise.all([
  loadPorts(),
  api('/api/snapshot').then(applySnapshot),
]).catch(error => showToast(error.message, true))
connectWebSocket()


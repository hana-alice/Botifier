(() => {
  'use strict';

  // Offline original samples. Time stamps express line starts, never word timing.
  const TRACKS = [
    { title: '晚风经过工作台', artist: 'Botifier Originals', language: 'zh', durationMs: 66000,
      lines: [
        [0, '窗边的光，慢慢变暖'], [5500, '把今天轻轻放一旁'],
        [11000, '让晚风替我\n按下暂停'], [16500, '听一首歌，再出发'],
        [22000, '街灯把影子拉得长'], [27500, '我们不必一直匆忙'],
        [33000, '抬头看，天还有光'], [38500, '远处有云，近处有你'],
        [44000, '让这一刻慢一点'], [49500, '让心跳跟着节拍'],
        [55000, '把未完的梦收好'], [60500, '明天见，晚风还在']
      ] },
    { title: 'Room for the evening', artist: 'Botifier Originals', language: 'en', durationMs: 66000,
      lines: [
        [0, 'The light leans\nthrough the window'], [5500, 'A little room\nto breathe'],
        [11000, 'Let the evening\ntake its time'], [16500, 'Leave the rush\nbehind'],
        [22000, 'The city hums\na softer tune'], [27500, 'We have nowhere\nto be'],
        [33000, 'Look up, the sky\nis still awake'], [38500, 'A quiet song\nfor you and me'],
        [44000, 'Take a breath\nbetween the lines'], [49500, 'Let the rhythm\nfind your feet'],
        [55000, 'Keep a little\nlight for later'], [60500, 'See you where\nthe evenings meet']
      ] }
  ].map(track => ({ ...track, lines: track.lines.map(([at, text]) => ({ at, text })) }));

  const $ = id => document.getElementById(id);
  const canvas = $('display');
  const ctx = canvas.getContext('2d');
  const media = window.matchMedia('(prefers-reduced-motion: reduce)');
  const state = {
    playing: !media.matches, trackIndex: 0, positionMs: 11000,
    connected: true, noLyrics: false, reducedMotion: media.matches,
    foreground: 'lyrics', interruption: null, anchor: performance.now(),
    message: '护眼与 NFC 动画先来先播，互不打断。'
  };
  const listeners = [];
  let raf = 0;
  let disposed = false;
  let lastLineKey = '';
  let currentVisual = 0;
  let liveDescription = '';

  const COLORS = { screen: '#080c0b', light: '#f4f5eb', secondary: '#a2aca3', dim: '#718076', accent: '#f7a288', line: '#29332d', green: '#91b89c', cyan: '#b4e7dd' };
  const font = '"Segoe UI", "Microsoft YaHei", sans-serif';
  const mono = '"Cascadia Code", Consolas, monospace';
  const track = () => TRACKS[state.trackIndex];
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
  const timecode = milliseconds => `${Math.floor(milliseconds / 60000)}:${String(Math.floor(milliseconds / 1000) % 60).padStart(2, '0')}`;
  function lineIndex(position = state.positionMs) {
    const lines = track().lines;
    let index = 0;
    for (let i = 1; i < lines.length; i += 1) { if (position < lines[i].at) break; index = i; }
    return index;
  }
  function advance(now = performance.now()) {
    if (state.playing) {
      state.positionMs = clamp(state.positionMs + now - state.anchor, 0, track().durationMs);
      if (state.positionMs >= track().durationMs) state.playing = false;
    }
    state.anchor = now;
    if (state.interruption && now - state.interruption.startedAt >= state.interruption.durationMs) endInterruption(false);
    state.foreground = state.interruption?.kind || (state.connected && !state.noLyrics ? 'lyrics' : 'idle');
  }
  function listen(target, event, handler, options) {
    target.addEventListener(event, handler, options);
    listeners.push(() => target.removeEventListener(event, handler, options));
  }
  function requestFrame() {
    if (!disposed && !document.hidden && !raf) raf = requestAnimationFrame(frame);
  }
  function cancelFrame() { if (raf) cancelAnimationFrame(raf); raf = 0; }
  function frame(now) {
    raf = 0;
    advance(now);
    render(now);
    if (state.playing || state.interruption || (!state.reducedMotion && currentVisual > 0)) requestFrame();
  }
  function update() { advance(); render(performance.now()); requestFrame(); }
  function describe(text) {
    if (liveDescription === text) return;
    liveDescription = text;
    $('screen-description').textContent = text;
    canvas.setAttribute('aria-label', text);
  }
  function text(value, x, y, size = 12, color = COLORS.light, weight = 400, align = 'left', family = font) {
    ctx.font = `${weight} ${size}px ${family}`;
    ctx.fillStyle = color;
    ctx.textAlign = align;
    ctx.textBaseline = 'middle';
    ctx.fillText(value, x, y);
  }
  function line(x1, y1, x2, y2, color = COLORS.line, width = 1) {
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.strokeStyle = color; ctx.lineWidth = width; ctx.stroke();
  }
  function roundedRect(x, y, width, height, radius, color) {
    ctx.beginPath(); ctx.roundRect(x, y, width, height, radius); ctx.fillStyle = color; ctx.fill();
  }
  function renderHeader(label, right, color = COLORS.secondary) {
    roundedRect(16, 16, 4, 4, 2, color);
    text(label, 26, 19, 8, color, 600, 'left', mono);
    text(right, 304, 19, 8, COLORS.secondary, 400, 'right', mono);
  }
  function splitFit(value, size, width) {
    ctx.font = `600 ${size}px ${font}`;
    const hardLines = value.split('\n');
    const result = [];
    for (const original of hardLines) {
      if (ctx.measureText(original).width <= width) { result.push(original); continue; }
      const words = track().language === 'en' ? original.split(' ') : Array.from(original);
      let current = '';
      for (const word of words) {
        const next = current ? `${current}${track().language === 'en' ? ' ' : ''}${word}` : word;
        if (current && ctx.measureText(next).width > width) { result.push(current); current = word; }
        else current = next;
      }
      if (current) result.push(current);
    }
    return result.slice(0, 2);
  }
  function fittedLines(value, preferred = 28) {
    let size = preferred;
    let lines = splitFit(value, size, 284);
    // Original samples are authored for two lines; this keeps future edits safe.
    while (size > 24 && value.split('\n').some(part => { ctx.font = `600 ${size}px ${font}`; return ctx.measureText(part).width > 284; })) {
      size -= 1; lines = splitFit(value, size, 284);
    }
    return { lines, size };
  }
  function drawLyrics(now) {
    const index = lineIndex();
    const current = track().lines[index];
    const key = `${state.trackIndex}:${index}:lyrics`;
    if (key !== lastLineKey) { lastLineKey = key; currentVisual = state.reducedMotion ? 0 : 1; }
    currentVisual = state.reducedMotion ? 0 : Math.max(0, currentVisual - .055);
    renderHeader('LYRICS', state.playing ? 'SIM / PLAYING' : 'SIM / PAUSED', state.playing ? COLORS.green : COLORS.secondary);
    const previous = track().lines[index - 1];
    if (previous) text(previous.text.replace('\n', ' '), 20, 55, 11, COLORS.dim);
    const { lines, size } = fittedLines(current.text);
    const lineHeight = size + 10;
    const top = 111 - ((lines.length - 1) * lineHeight / 2);
    ctx.save();
    ctx.translate(0, currentVisual * 6);
    ctx.globalAlpha = 1 - currentVisual * .4;
    lines.forEach((value, i) => text(value, 19, top + i * lineHeight, size, COLORS.light, 600));
    ctx.restore();
    roundedRect(19, top + (lines.length - 1) * lineHeight + 25, 29, 3, 1.5, COLORS.accent);
    const next = track().lines[index + 1];
    if (next) text(next.text.replace('\n', ' '), 20, 177, 11, COLORS.dim);
    line(18, 201, 302, 201);
    text(track().title, 19, 217, 9, COLORS.secondary, 500);
    text(timecode(state.positionMs), 301, 217, 9, COLORS.secondary, 400, 'right', mono);
    roundedRect(19, 232, 282, 1, 0, COLORS.line);
    roundedRect(19, 232, 282 * state.positionMs / track().durationMs, 1, 0, COLORS.accent);
    describe(`模拟${state.playing ? '播放' : '暂停'}，${track().title}，当前歌词：${current.text.replace('\n', '，')}`);
    $('robot-head').style.transform = '';
  }
  function eyes(now, kind = 'calm', offset = 0) {
    const reduced = state.reducedMotion;
    const blink = !reduced && Math.floor(now / 3200) % 3 === 0 && now % 3200 < 120;
    const height = blink ? 3 : 25;
    ctx.lineWidth = 7; ctx.strokeStyle = COLORS.cyan; ctx.lineCap = 'round';
    if (kind === 'happy') {
      [119, 201].forEach(x => { ctx.beginPath(); ctx.moveTo(x - 16, 113); ctx.quadraticCurveTo(x, 89, x + 16, 113); ctx.stroke(); });
      ctx.beginPath(); ctx.arc(160, 126, 12, .2, Math.PI - .2); ctx.stroke();
    } else {
      roundedRect(108 + offset, 89, 16, height, 7, COLORS.cyan);
      roundedRect(195 + offset, 89, 16, height, 7, COLORS.cyan);
      ctx.lineWidth = 3;
      ctx.beginPath(); ctx.arc(160 + offset / 2, 126, 8, .2, Math.PI - .2); ctx.stroke();
    }
  }
  function drawIdle(now) {
    lastLineKey = '';
    const disconnected = !state.connected;
    renderHeader('LYRICS / STANDBY', 'SIMULATION');
    eyes(now, 'calm');
    text(disconnected ? '等待播放源连接' : '这首歌，暂时没有歌词', 160, 168, 17, COLORS.light, 600, 'center');
    text(disconnected ? '连接恢复后，跟上当前进度' : '保留歌曲信息，不猜测歌词', 160, 193, 10, COLORS.secondary, 400, 'center');
    line(18, 211, 302, 211);
    text(track().title, 19, 226, 9, COLORS.secondary);
    text(timecode(state.positionMs), 301, 226, 9, COLORS.secondary, 400, 'right', mono);
    describe(`${disconnected ? '模拟播放源已断开' : '模拟歌曲没有歌词'}，音乐模拟进度${timecode(state.positionMs)}`);
    $('robot-head').style.transform = '';
  }
  function drawInterruption(now) {
    lastLineKey = '';
    const { kind, startedAt, durationMs } = state.interruption;
    const elapsed = now - startedAt;
    if (kind === 'touch') {
      renderHeader('PET / HELLO', 'CONCEPT');
      eyes(now, 'happy');
      text('嘿，我在呢。', 160, 171, 20, COLORS.light, 600, 'center');
      text('摸头互动概念 · 音乐继续', 160, 203, 10, COLORS.secondary, 400, 'center');
      $('robot-head').style.transform = state.reducedMotion ? '' : `rotate(${Math.sin(elapsed / 500) * 3}deg)`;
      describe('摸头互动概念：开心的表情。音乐进度继续，结束后返回当前歌词。');
    } else if (kind === 'nfc') {
      renderHeader('NFC / ANIMATION', 'CONCEPT');
      const phase = state.reducedMotion ? 1 : clamp(elapsed / 700, 0, 1);
      ctx.save(); ctx.translate(160, 105); ctx.scale(.86 + phase * .14, .86 + phase * .14);
      roundedRect(-33, -40, 66, 80, 9, '#dcece3');
      roundedRect(-17, -16, 34, 28, 4, '#2c5545');
      line(-11, -9, 11, 6, '#dcece3', 3);
      line(-11, 6, 11, -9, '#dcece3', 3);
      ctx.restore();
      text('一触，即刻开场。', 160, 174, 20, COLORS.light, 600, 'center');
      text('NFC 动画占位 · 当前动画完整播放', 160, 204, 10, COLORS.secondary, 400, 'center');
      $('robot-head').style.transform = '';
      describe('NFC 概念动画正在播放；护眼和其他动画不会打断当前动画。');
    } else if (elapsed < 2200) {
      renderHeader('EYE CARE / INTRO', 'CONCEPT');
      const angle = state.reducedMotion ? .25 : elapsed / 1800;
      ctx.save(); ctx.translate(160, 94); ctx.rotate(angle * .25);
      ctx.fillStyle = '#294a47'; ctx.beginPath(); ctx.moveTo(0, -43); ctx.lineTo(43, -15); ctx.lineTo(35, 30); ctx.lineTo(-6, 47); ctx.lineTo(-42, 16); ctx.lineTo(-36, -25); ctx.closePath(); ctx.fill();
      ctx.strokeStyle = COLORS.cyan; ctx.lineWidth = 1.5; ctx.stroke();
      line(0, -43, -6, 47, '#80b3ac'); line(-42, 16, 43, -15, '#80b3ac'); line(-36, -25, 35, 30, '#80b3ac');
      ctx.restore();
      text('先把目光，交给远方。', 160, 166, 20, COLORS.light, 600, 'center');
      text('3D 动画概念占位 · 非守岸人素材', 160, 200, 10, COLORS.secondary, 400, 'center');
      $('robot-head').style.transform = '';
      describe('护眼片头概念占位；此处没有真实守岸人 3D 动画素材。');
    } else if (elapsed < 6200) {
      renderHeader('EYE CARE / MOVE', 'CONCEPT');
      const phase = (elapsed - 2200) / 4000;
      const offset = state.reducedMotion ? 0 : Math.sin(phase * Math.PI * 8) * 11;
      eyes(now, 'calm', offset);
      text('跟着我，放松一下。', 160, 169, 20, COLORS.light, 600, 'center');
      text('表情与机头配合示意 · 非实体动作', 160, 202, 10, COLORS.secondary, 400, 'center');
      $('robot-head').style.transform = state.reducedMotion ? '' : `rotate(${offset / 3}deg)`;
      describe('护眼动作概念：眼神与机器人摇头配合示意；这不是实体机头的运动。');
    } else {
      renderHeader('EYE CARE / REST', '20–20–20');
      text('看向远处，让眼睛休息', 160, 66, 14, COLORS.secondary, 400, 'center');
      const remaining = Math.max(1, Math.ceil((durationMs - elapsed) / 1000));
      text(String(remaining).padStart(2, '0'), 160, 124, 70, COLORS.cyan, 500, 'center', mono);
      text('REST 20', 160, 181, 11, COLORS.cyan, 500, 'center', mono);
      text('音乐继续，结束后跟上当前歌词', 160, 213, 10, COLORS.secondary, 400, 'center');
      $('robot-head').style.transform = '';
      describe(`护眼休息倒计时，剩余${remaining}秒。音乐进度继续。`);
    }
    roundedRect(19, 234, 282 * clamp(elapsed / durationMs, 0, 1), 2, 1, COLORS.cyan);
  }
  function render(now) {
    if (disposed) return;
    ctx.clearRect(0, 0, 320, 240);
    ctx.fillStyle = COLORS.screen; ctx.fillRect(0, 0, 320, 240);
    if (state.interruption) drawInterruption(now);
    else if (!state.connected || state.noLyrics) drawIdle(now);
    else drawLyrics(now);
    syncControls();
  }
  function syncControls() {
    $('timeline').max = String(track().durationMs);
    $('timeline').value = String(Math.round(state.positionMs));
    $('timeline').setAttribute('aria-valuetext', `${timecode(state.positionMs)}，总长${timecode(track().durationMs)}`);
    $('elapsed').textContent = timecode(state.positionMs);
    $('duration').textContent = timecode(track().durationMs);
    $('play-label').textContent = state.playing ? '暂停' : '播放';
    $('play-toggle').setAttribute('aria-label', state.playing ? '暂停模拟播放' : '开始模拟播放');
    $('play-icon').innerHTML = state.playing ? '<path d="M8 6v12m8-12v12"/>' : '<path d="m8 5 11 7-11 7z" fill="currentColor" stroke-width="1"/>';
    $('track-title').textContent = track().title;
    $('track-artist').textContent = track().artist;
    $('album-art').classList.toggle('english', state.trackIndex === 1);
    document.querySelectorAll('[data-track]').forEach(button => button.setAttribute('aria-pressed', String(Number(button.dataset.track) === state.trackIndex)));
    const foregroundName = { lyrics: state.playing ? '歌词播放' : '歌词已暂停', eye: '护眼提醒 · 受保护', nfc: 'NFC 动画 · 受保护', touch: '摸头互动', idle: !state.connected ? '等待播放源' : '无歌词 · 信息保留' };
    $('foreground-label').textContent = foregroundName[state.foreground];
    $('stage-message').textContent = state.interruption ? '歌词稍候，你更重要。' : !state.connected ? '连接恢复，自动跟上。' : state.noLyrics ? '没有歌词，也不必空白。' : '此刻，只听这一句。';
    $('event-message').textContent = state.message;
    $('finish-interruption').hidden = !state.interruption;
    ['eye', 'nfc', 'touch'].forEach(kind => $('trigger-' + kind).setAttribute('aria-pressed', String(state.interruption?.kind === kind)));
    $('connected').checked = state.connected;
    $('no-lyrics').checked = state.noLyrics;
    $('reduced-motion').checked = state.reducedMotion;
    document.body.classList.toggle('reduced-motion', state.reducedMotion);
  }
  function setPosition(value) {
    advance();
    state.positionMs = clamp(Number.isFinite(Number(value)) ? Number(value) : 0, 0, track().durationMs);
    state.anchor = performance.now();
    update();
    return getState();
  }
  function setPlaying(value) {
    advance();
    if (value && state.positionMs >= track().durationMs) state.positionMs = 0;
    state.playing = Boolean(value); state.anchor = performance.now(); update();
    return getState();
  }
  function setTrack(index) {
    advance();
    state.trackIndex = clamp(Math.trunc(Number(index) || 0), 0, TRACKS.length - 1);
    state.positionMs = 0; state.anchor = performance.now(); lastLineKey = '';
    update(); return getState();
  }
  function setConnected(value) { advance(); state.connected = Boolean(value); update(); return getState(); }
  function setNoLyrics(value) { advance(); state.noLyrics = Boolean(value); update(); return getState(); }
  function setReducedMotion(value) { advance(); state.reducedMotion = Boolean(value); currentVisual = 0; update(); return getState(); }
  function interrupt(kind) {
    advance();
    if (!['eye', 'nfc', 'touch'].includes(kind)) return false;
    const active = state.interruption;
    if (active && (active.kind === 'eye' || active.kind === 'nfc')) {
      state.message = `当前${active.kind === 'eye' ? '护眼流程' : 'NFC 动画'}完整播放；本次触发忽略，不排队。`;
      update(); return false;
    }
    if (active && active.kind === kind) {
      state.message = '当前互动正在播放，本次触发忽略。'; update(); return false;
    }
    state.interruption = { kind, startedAt: performance.now(), durationMs: { eye: 26200, nfc: 4600, touch: 3200 }[kind] };
    state.foreground = kind;
    state.message = kind === 'eye' ? '片头占位 → 动作示意 → REST 20；音乐继续。' : kind === 'nfc' ? 'NFC 动画概念；播放中不接受其他插播。' : '摸头互动优先于歌词，结束后跟上歌曲。';
    update(); return true;
  }
  function endInterruption(manual = true) {
    if (!state.interruption) return false;
    state.interruption = null; lastLineKey = '';
    state.message = manual ? '已结束插播；歌词恢复到音乐的当前位置。' : '插播结束；歌词已跟上音乐的当前位置。';
    if (manual) update();
    return true;
  }
  function getState() {
    advance();
    return { playing: state.playing, trackIndex: state.trackIndex, positionMs: state.positionMs,
      currentLine: lineIndex(), foreground: state.foreground, reducedMotion: state.reducedMotion,
      connected: state.connected, noLyrics: state.noLyrics };
  }
  function sampleAt(milliseconds) {
    // Deterministic capture: freeze music and return the original lyric at a time.
    setPlaying(false); setPosition(milliseconds);
    return { ...getState(), lyric: track().lines[lineIndex()].text };
  }

  listen($('play-toggle'), 'click', () => setPlaying(!state.playing));
  listen($('timeline'), 'input', event => setPosition(event.target.value));
  listen($('previous-line'), 'click', () => { advance(); const index = lineIndex(); setPosition(track().lines[Math.max(0, index - 1)].at); });
  listen($('next-line'), 'click', () => { advance(); setPosition(track().lines[Math.min(track().lines.length - 1, lineIndex() + 1)].at); });
  document.querySelectorAll('[data-track]').forEach(button => listen(button, 'click', () => setTrack(button.dataset.track)));
  ['eye', 'nfc', 'touch'].forEach(kind => listen($('trigger-' + kind), 'click', () => interrupt(kind)));
  listen($('finish-interruption'), 'click', () => endInterruption());
  listen($('connected'), 'change', event => setConnected(event.target.checked));
  listen($('no-lyrics'), 'change', event => setNoLyrics(event.target.checked));
  listen($('reduced-motion'), 'change', event => setReducedMotion(event.target.checked));
  listen(document, 'visibilitychange', () => { if (document.hidden) { advance(); cancelFrame(); } else update(); });
  listen(media, 'change', event => { setReducedMotion(event.matches); if (event.matches) setPlaying(false); });
  listen(window, 'pagehide', () => { advance(); cancelFrame(); disposed = true; listeners.splice(0).forEach(remove => remove()); });

  window.lyricsLab = Object.freeze({ getState, setPosition, setPlaying, setTrack, setConnected, setNoLyrics,
    interrupt, finishInterruption: endInterruption, setReducedMotion, sampleAt });
  update();
})();

<script>
  import { onMount, tick } from 'svelte';

  // Values mirror VirtualKey in firm/lib/espercore/include/esper-core/remote.h.
  const key = {
    display: 24, dimmer: 12, info: 14, shuffle: 13,
    discPrevious: 19, discNext: 18, seekBack: 21, seekForward: 20,
    trackPrevious: 22, trackNext: 23, play: 26, pause: 27, stop: 28, eject: 29,
    cd: 31, radio: 32, bluetooth: 33, settings: 34,
    up: 36, down: 37, left: 38, right: 39, enter: 40
  };

  const modes = [
    { label: 'CD', key: key.cd },
    { label: 'Radio', key: key.radio },
    { label: 'Settings', key: key.settings }
  ];
  const utility = [
    { label: 'Display', key: key.display },
    { label: 'Dimmer', key: key.dimmer },
    { label: 'Info', key: key.info },
    { label: 'Shuffle', key: key.shuffle },
  ];
  // ModeSelection values from firm/include/mode_host.h.
  const MODE_CD = 0;
  const MODE_RADIO = 1;
  const MODE_BT = 2;  // unreachable because conflicts with wifi, just for posterity
  const MODE_SETTINGS = 3;

  const mode_names = {
    [MODE_CD]: "Compact Disc",
    [MODE_RADIO]: "Internet Radio",
    [MODE_BT]: "Bluetooth",
    [MODE_SETTINGS]: "Configuration"
  };

  const digits = [1, 2, 3, 4, 5, 6, 7, 8, 9];

  let canvas;
  let socket;
  let retryTimer;
  let active = true;
  let connection = 'connecting';
  let detail = 'Connecting…';
  let width = 160;
  let height = 32;
  let hasFrame = false;
  let currentMode = null;
  let seekPress = null;
  let seekReleasePending = false;

  function setMode(mode) {
    if (!Number.isInteger(mode)) return;
    currentMode = mode;
    detail = "ESPer CDP · " + mode_names[mode];
    if (mode !== MODE_CD && seekPress) {
      seekPress = null;
      seekReleasePending = false;
    }
  }

  function drawFrame(bytes) {
    const stride = Math.ceil(height / 8);
    if (!canvas || bytes.length !== width * stride) return;
    const context = canvas.getContext('2d', { alpha: false });
    if (!context) return;
    const image = context.createImageData(width, height);
    const pixels = image.data;
    for (let x = 0; x < width; x++) {
      for (let y = 0; y < height; y++) {
        const lit = (bytes[x * stride + (y >> 3)] & (0x80 >> (y & 7))) !== 0;
        const offset = (y * width + x) * 4;
        pixels[offset] = lit ? 149 : 8;
        pixels[offset + 1] = lit ? 239 : 22;
        pixels[offset + 2] = lit ? 206 : 22;
        pixels[offset + 3] = 255;
      }
    }
    context.putImageData(image, 0, 0);
    hasFrame = true;
  }

  function scheduleReconnect() {
    if (!active || retryTimer) return;
    retryTimer = setTimeout(() => {
      retryTimer = undefined;
      connect();
    }, 2000);
  }

  async function connect() {
    connection = 'connecting';
    detail = 'Connecting…';
    try {
      const response = await fetch('/api/status', { cache: 'no-store' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const status = await response.json();
      if (!Number.isInteger(status.width) || !Number.isInteger(status.height) ||
          status.width <= 0 || status.height <= 0 || status.format !== 'vertical-1bpp-msb') {
        throw new Error('Unsupported display format');
      }
      if (!active) return;
      width = status.width;
      height = status.height;
      setMode(status.mode);
      hasFrame = false;
      await tick();
      const url = new URL(status.websocket || '/ws', window.location.href);
      url.protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(url);
      socket.binaryType = 'arraybuffer';
      socket.onopen = () => {
        connection = 'connected';
        detail = 'ESPer CDP';
        if (seekReleasePending && sendKey(key.play)) seekReleasePending = false;
      };
      socket.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
          drawFrame(new Uint8Array(event.data));
        } else if (typeof event.data === 'string') {
          try {
            const message = JSON.parse(event.data);
            if (message?.type === 'mode') setMode(message.mode);
          } catch {
            // Ignore unknown text messages.
          }
        }
      };
      socket.onclose = () => {
        if (!active) return;
        connection = 'disconnected';
        currentMode = null;
        detail = 'Player unavailable · retrying';
        scheduleReconnect();
      };
      socket.onerror = () => socket.close();
    } catch (error) {
      if (!active) return;
      connection = 'disconnected';
      detail = `${error.message || 'Connection failed'} · retrying`;
      scheduleReconnect();
    }
  }

  function sendKey(code) {
    if (socket?.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify({ type: 'key', key: code }));
      return true;
    }
    return false;
  }

  function sendDigit(digit) {
    sendKey(digit + 1); // RVK_NUM_0 is 1, through RVK_NUM_9 at 10.
  }

  function beginPointerSeek(event, code) {
    if (event.button !== 0 || seekPress || !sendKey(code)) return;
    seekPress = { type: 'pointer', id: event.pointerId };
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function endPointerSeek(event) {
    if (seekPress?.type !== 'pointer' || seekPress.id !== event.pointerId) return;
    releaseSeek();
  }

  function beginKeyboardSeek(event, code) {
    if (event.key !== ' ' && event.key !== 'Enter') return;
    event.preventDefault();
    if (event.repeat || seekPress || !sendKey(code)) return;
    seekPress = { type: 'keyboard', key: event.key };
  }

  function endKeyboardSeek(event) {
    if (seekPress?.type !== 'keyboard' || seekPress.key !== event.key) return;
    event.preventDefault();
    releaseSeek();
  }

  function endSeekOnBlur() {
    if (seekPress?.type !== 'keyboard') return;
    releaseSeek();
  }

  function releaseSeek() {
    if (!seekPress) return;
    seekPress = null;
    if (!sendKey(key.play)) seekReleasePending = true;
  }

  onMount(() => {
    connect();
    window.addEventListener('blur', releaseSeek);
    return () => {
      releaseSeek();
      active = false;
      clearTimeout(retryTimer);
      window.removeEventListener('blur', releaseSeek);
      socket?.close();
    };
  });
</script>

<svelte:head>
  <meta name="description" content="Live display and remote control for ESPer CDP" />
</svelte:head>

<main class="remote">
  <section class="display-panel" aria-label="Player display">
    <div class="display-glass">
      <canvas bind:this={canvas} {width} {height}></canvas>
      {#if !hasFrame}<span class="display-wait">Waiting for display</span>{/if}
    </div>
    <p class="display-caption">{detail}</p>
  </section>

  {#if currentMode === MODE_CD}
  <section class="section" aria-label="Playback controls">
    <h2>Transport</h2>
    <div class="transport-rows">
      <div class="transport-row transport-main">
        <button onclick={() => sendKey(key.play)} disabled={connection !== 'connected'} aria-label="Play"><strong>▶</strong><span>Play</span></button>
        <button onclick={() => sendKey(key.pause)} disabled={connection !== 'connected'} aria-label="Pause"><strong>⏸</strong><span>Pause</span></button>
        <button onclick={() => sendKey(key.stop)} disabled={connection !== 'connected'} aria-label="Stop"><strong>■</strong><span>Stop</span></button>
      </div>
      <div class="transport-row transport-seek">
        <button onclick={() => sendKey(key.trackPrevious)} disabled={connection !== 'connected'} aria-label="Previous track"><strong>⏮</strong><span>Previous</span></button>
        <button onpointerdown={(event) => beginPointerSeek(event, key.seekBack)} onpointerup={endPointerSeek} onpointercancel={endPointerSeek} onlostpointercapture={endPointerSeek} onkeydown={(event) => beginKeyboardSeek(event, key.seekBack)} onkeyup={endKeyboardSeek} onblur={endSeekOnBlur} disabled={connection !== 'connected'} aria-label="Seek back"><strong>◀◀</strong><span>Seek back</span></button>
        <button onpointerdown={(event) => beginPointerSeek(event, key.seekForward)} onpointerup={endPointerSeek} onpointercancel={endPointerSeek} onlostpointercapture={endPointerSeek} onkeydown={(event) => beginKeyboardSeek(event, key.seekForward)} onkeyup={endKeyboardSeek} onblur={endSeekOnBlur} disabled={connection !== 'connected'} aria-label="Seek forward"><strong>▶▶</strong><span>Seek forward</span></button>
        <button onclick={() => sendKey(key.trackNext)} disabled={connection !== 'connected'} aria-label="Next track"><strong>⏭</strong><span>Next</span></button>
      </div>
      <div class="transport-row transport-disc">
        <button onclick={() => sendKey(key.discPrevious)} disabled={connection !== 'connected'}>Disc −</button>
        <button onclick={() => sendKey(key.eject)} disabled={connection !== 'connected'}>Eject ⏏</button>
        <button onclick={() => sendKey(key.discNext)} disabled={connection !== 'connected'}>Disc +</button>
      </div>
    </div>
  </section>
  {/if}

  <section class="section lower" aria-label="Navigation and other controls">
    <div class="navigation">
    {#if currentMode === MODE_SETTINGS || currentMode === MODE_RADIO}
      <h2>Numbers</h2>
      <div class="number-pad">
        {#each digits as digit}
          <button onclick={() => sendDigit(digit)} disabled={connection !== 'connected'}>{digit}</button>
        {/each}
        <button class="zero" onclick={() => sendDigit(0)} disabled={connection !== 'connected'}>0</button>
      </div>
    {/if}
    {#if currentMode === MODE_SETTINGS}
      <h2>Navigate</h2>
      <div class="dpad">
        <button class="up" onclick={() => sendKey(key.up)} disabled={connection !== 'connected'} aria-label="Up">▲</button>
        <button class="left" onclick={() => sendKey(key.left)} disabled={connection !== 'connected'} aria-label="Left">◀</button>
        <button class="enter" onclick={() => sendKey(key.enter)} disabled={connection !== 'connected'}>OK</button>
        <button class="right" onclick={() => sendKey(key.right)} disabled={connection !== 'connected'} aria-label="Right">▶</button>
        <button class="down" onclick={() => sendKey(key.down)} disabled={connection !== 'connected'} aria-label="Down">▼</button>
      </div>
    {/if}
    </div>

    <section class="section" aria-label="Source">
    <h2>Source</h2>
    <div class="mode-grid">
      {#each modes as button}
        <button class="mode-button" onclick={() => sendKey(button.key)} disabled={connection !== 'connected'}>{button.label}</button>
      {/each}
    </div>
    </section>

    <div class="more">
      <h2>More</h2>
      <div class="utility-grid">
        {#each utility as button}
          <button onclick={() => sendKey(button.key)} disabled={connection !== 'connected'}>{button.label}</button>
        {/each}
      </div>
    </div>
  </section>
</main>

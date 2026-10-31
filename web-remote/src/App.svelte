<script lang="ts">
  import { onMount, tick } from 'svelte';
  import { ConnectionState, ModeSelection, VirtualKey, digitKeys, isModeSelection } from './protocol';
  import SettingsEditor from './SettingsEditor.svelte';

  type DeviceStatus = {
    width: number;
    height: number;
    format: 'vertical-1bpp-msb';
    websocket?: string;
    mode?: unknown;
  };
  type SeekPress = { type: 'pointer'; id: number } | { type: 'keyboard'; key: string };

  const modes = [
    { label: 'CD', key: VirtualKey.ModeCD },
    { label: 'Radio', key: VirtualKey.ModeRadio },
    { label: 'Settings', key: VirtualKey.ModeSettings }
  ];
  const utility = [
    { label: 'Display', key: VirtualKey.Display },
    { label: 'Dimmer', key: VirtualKey.Dimmer },
    { label: 'Info', key: VirtualKey.Info },
    { label: 'Shuffle', key: VirtualKey.Shuffle }
  ];
  const modeNames: Record<ModeSelection, string> = {
    [ModeSelection.CD]: 'Compact Disc',
    [ModeSelection.Radio]: 'Internet Radio',
    [ModeSelection.Bluetooth]: 'Bluetooth',
    [ModeSelection.Settings]: 'Configuration',
    [ModeSelection.Standby]: 'Power Off'
  };

  let canvas: HTMLCanvasElement | undefined;
  let socket: WebSocket | undefined;
  let retryTimer: number | undefined;
  let active = true;
  let connection = ConnectionState.Connecting;
  let detail = 'Connecting…';
  let width = 160;
  let height = 32;
  let hasFrame = false;
  let currentMode: ModeSelection | null = null;
  let seekPress: SeekPress | null = null;
  let seekReleasePending = false;
  let view: 'remote' | 'settings' = 'remote';

  function isDeviceStatus(value: unknown): value is DeviceStatus {
    if (typeof value !== 'object' || value === null) return false;
    const status = value as Record<string, unknown>;
    return Number.isInteger(status.width) && Number.isInteger(status.height) &&
      (status.width as number) > 0 && (status.height as number) > 0 &&
      status.format === 'vertical-1bpp-msb' &&
      (status.websocket === undefined || typeof status.websocket === 'string');
  }

  function setMode(mode: unknown) {
    if (!isModeSelection(mode)) return;
    currentMode = mode;
    detail = `ESPer CDP · ${modeNames[mode]}`;
    if (mode !== ModeSelection.CD && seekPress) {
      seekPress = null;
      seekReleasePending = false;
    }
  }

  function drawFrame(bytes: Uint8Array) {
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
    if (!active || retryTimer !== undefined) return;
    retryTimer = window.setTimeout(() => {
      retryTimer = undefined;
      void connect();
    }, 2000);
  }

  async function connect() {
    connection = ConnectionState.Connecting;
    detail = 'Connecting…';
    try {
      const response = await fetch('/api/status', { cache: 'no-store' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const status: unknown = await response.json();
      if (!isDeviceStatus(status)) throw new Error('Unsupported display format');
      if (!active) return;
      width = status.width;
      height = status.height;
      setMode(status.mode);
      hasFrame = false;
      await tick();
      const url = new URL(status.websocket ?? '/ws', window.location.href);
      url.protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(url);
      socket.binaryType = 'arraybuffer';
      socket.onopen = () => {
        connection = ConnectionState.Connected;
        detail = currentMode === null ? 'ESPer CDP' : `ESPer CDP · ${modeNames[currentMode]}`;
        if (seekReleasePending && sendKey(VirtualKey.Play)) seekReleasePending = false;
      };
      socket.onmessage = (event: MessageEvent) => {
        if (event.data instanceof ArrayBuffer) {
          drawFrame(new Uint8Array(event.data));
        } else if (typeof event.data === 'string') {
          try {
            const message = JSON.parse(event.data) as { type?: string; mode?: unknown };
            if (message?.type === 'mode') setMode(message.mode);
          } catch {
            // Ignore unknown text messages.
          }
        }
      };
      socket.onclose = () => {
        if (!active) return;
        connection = ConnectionState.Disconnected;
        currentMode = null;
        detail = 'Player unavailable · retrying';
        scheduleReconnect();
      };
      socket.onerror = () => socket?.close();
    } catch (error) {
      if (!active) return;
      connection = ConnectionState.Disconnected;
      detail = `${error instanceof Error ? error.message : 'Connection failed'} · retrying`;
      scheduleReconnect();
    }
  }

  function sendKey(code: VirtualKey): boolean {
    if (socket?.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify({ type: 'key', key: code }));
      return true;
    }
    return false;
  }

  function sendDigit(digit: number) {
    const code = digitKeys[digit];
    if (code !== undefined) sendKey(code);
  }

  function beginPointerSeek(event: PointerEvent, code: VirtualKey) {
    if (event.button !== 0 || seekPress || !sendKey(code)) return;
    seekPress = { type: 'pointer', id: event.pointerId };
    (event.currentTarget as HTMLButtonElement).setPointerCapture(event.pointerId);
  }

  function endPointerSeek(event: PointerEvent) {
    if (seekPress?.type !== 'pointer' || seekPress.id !== event.pointerId) return;
    releaseSeek();
  }

  function beginKeyboardSeek(event: KeyboardEvent, code: VirtualKey) {
    if (event.key !== ' ' && event.key !== 'Enter') return;
    event.preventDefault();
    if (event.repeat || seekPress || !sendKey(code)) return;
    seekPress = { type: 'keyboard', key: event.key };
  }

  function endKeyboardSeek(event: KeyboardEvent) {
    if (seekPress?.type !== 'keyboard' || seekPress.key !== event.key) return;
    event.preventDefault();
    releaseSeek();
  }

  function endSeekOnBlur() {
    if (seekPress?.type === 'keyboard') releaseSeek();
  }

  function releaseSeek() {
    if (!seekPress) return;
    seekPress = null;
    if (!sendKey(VirtualKey.Play)) seekReleasePending = true;
  }

  onMount(() => {
    void connect();
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

  <nav class="view-tabs" aria-label="Page">
    <button class:active={view === 'remote'} aria-pressed={view === 'remote'} onclick={() => view = 'remote'}>Remote</button>
    <button class:active={view === 'settings'} aria-pressed={view === 'settings'} onclick={() => view = 'settings'}>Settings</button>
  </nav>

  {#if view === 'settings'}
    <SettingsEditor connected={connection === ConnectionState.Connected} />
  {:else}
  {#if currentMode === ModeSelection.CD}
  <section class="section" aria-label="Playback controls">
    <h2>Transport</h2>
    <div class="transport-rows">
      <div class="transport-row transport-main">
        <button onclick={() => sendKey(VirtualKey.Play)} disabled={connection !== 'connected'} aria-label="Play"><strong>▶</strong><span>Play</span></button>
        <button onclick={() => sendKey(VirtualKey.Pause)} disabled={connection !== 'connected'} aria-label="Pause"><strong>⏸</strong><span>Pause</span></button>
        <button onclick={() => sendKey(VirtualKey.Stop)} disabled={connection !== 'connected'} aria-label="Stop"><strong>■</strong><span>Stop</span></button>
      </div>
      <div class="transport-row transport-seek">
        <button onclick={() => sendKey(VirtualKey.TrackPrevious)} disabled={connection !== 'connected'} aria-label="Previous track"><strong>⏮</strong><span>Previous</span></button>
        <button onpointerdown={(event) => beginPointerSeek(event, VirtualKey.SeekBack)} onpointerup={endPointerSeek} onpointercancel={endPointerSeek} onlostpointercapture={endPointerSeek} onkeydown={(event) => beginKeyboardSeek(event, VirtualKey.SeekBack)} onkeyup={endKeyboardSeek} onblur={endSeekOnBlur} disabled={connection !== 'connected'} aria-label="Seek back"><strong>◀◀</strong><span>Seek back</span></button>
        <button onpointerdown={(event) => beginPointerSeek(event, VirtualKey.SeekForward)} onpointerup={endPointerSeek} onpointercancel={endPointerSeek} onlostpointercapture={endPointerSeek} onkeydown={(event) => beginKeyboardSeek(event, VirtualKey.SeekForward)} onkeyup={endKeyboardSeek} onblur={endSeekOnBlur} disabled={connection !== 'connected'} aria-label="Seek forward"><strong>▶▶</strong><span>Seek forward</span></button>
        <button onclick={() => sendKey(VirtualKey.TrackNext)} disabled={connection !== 'connected'} aria-label="Next track"><strong>⏭</strong><span>Next</span></button>
      </div>
      <div class="transport-row transport-disc">
        <button onclick={() => sendKey(VirtualKey.DiscPrevious)} disabled={connection !== 'connected'}>Disc −</button>
        <button onclick={() => sendKey(VirtualKey.Eject)} disabled={connection !== 'connected'}>Eject ⏏</button>
        <button onclick={() => sendKey(VirtualKey.DiscNext)} disabled={connection !== 'connected'}>Disc +</button>
      </div>
    </div>
  </section>
  {/if}

  <section class="section lower" aria-label="Navigation and other controls">
    <div class="navigation">
    {#if currentMode === ModeSelection.Settings || currentMode === ModeSelection.Radio}
      <h2>Numbers</h2>
      <div class="number-pad">
        {#each [1, 2, 3, 4, 5, 6, 7, 8, 9] as digit}
          <button onclick={() => sendDigit(digit)} disabled={connection !== 'connected'}>{digit}</button>
        {/each}
        <button class="zero" onclick={() => sendDigit(0)} disabled={connection !== 'connected'}>0</button>
      </div>
    {/if}
    {#if currentMode === ModeSelection.Settings}
      <h2>Navigate</h2>
      <div class="dpad">
        <button class="up" onclick={() => sendKey(VirtualKey.Up)} disabled={connection !== 'connected'} aria-label="Up">▲</button>
        <button class="left" onclick={() => sendKey(VirtualKey.Left)} disabled={connection !== 'connected'} aria-label="Left">◀</button>
        <button class="enter" onclick={() => sendKey(VirtualKey.Enter)} disabled={connection !== 'connected'}>OK</button>
        <button class="right" onclick={() => sendKey(VirtualKey.Right)} disabled={connection !== 'connected'} aria-label="Right">▶</button>
        <button class="down" onclick={() => sendKey(VirtualKey.Down)} disabled={connection !== 'connected'} aria-label="Down">▼</button>
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
  {/if}
</main>

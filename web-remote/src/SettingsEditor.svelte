<script lang="ts">
  import { settingsMenu, type SettingField, type SettingGroup } from './settings-menu';

  export let connected: boolean;

  type Preference =
    | { key: string; type: 'bool'; value: boolean; password: boolean }
    | { key: string; type: 'int'; value: number; password: boolean }
    | { key: string; type: 'string'; value: string; password: boolean };
  const passwordMask = '********';

  let preferences: Record<string, Preference> = {};
  let path: SettingGroup[] = [];
  let editing: SettingField | null = null;
  let draft = '';
  let selection = 0;
  let loading = false;
  let loaded = false;
  let attempted = false;
  let savingKey: string | null = null;
  let error = '';
  let notice = '';

  $: currentGroup = path.at(-1) ?? settingsMenu;
  $: dirty = editing ? isDirty(preferences[editing.key], draft, selection) : false;
  $: if (connected && !attempted) {
    attempted = true;
    void loadPreferences();
  }

  function isPreference(value: unknown): value is Preference {
    if (typeof value !== 'object' || value === null) return false;
    const item = value as Record<string, unknown>;
    return typeof item.key === 'string' && typeof item.password === 'boolean' && (
      (item.type === 'bool' && typeof item.value === 'boolean') ||
      (item.type === 'int' && Number.isInteger(item.value)) ||
      (item.type === 'string' && typeof item.value === 'string')
    );
  }

  async function loadPreferences() {
    if (loading) return;
    loading = true;
    error = '';
    try {
      const response = await fetch('/api/preferences', { cache: 'no-store' });
      if (!response.ok) throw new Error(`Could not load settings (HTTP ${response.status})`);
      const data: unknown = await response.json();
      if (!Array.isArray(data)) throw new Error('Invalid settings response');
      const next: Record<string, Preference> = {};
      for (const item of data) {
        if (isPreference(item)) next[item.key] = item;
      }
      if (Object.keys(next).length === 0) throw new Error('No settings returned by device');
      preferences = next;
      loaded = true;
    } catch (cause) {
      error = cause instanceof Error ? cause.message : 'Could not load settings';
    } finally {
      loading = false;
    }
  }

  function groupLabel(group: SettingGroup, values: Record<string, Preference>): string {
    if (group.presetIndex === undefined) return group.label;
    const name = values[`netr_n${group.presetIndex}`];
    return name?.type === 'string' && name.value ? `${group.presetIndex + 1}: ${name.value}` : group.label;
  }

  function summary(preference: Preference, field: SettingField): string {
    if (preference.type === 'bool') return preference.value ? 'On' : 'Off';
    if (preference.type === 'int') {
      return field.options?.find((option) => option.value === preference.value)?.label ?? String(preference.value);
    }
    if (preference.password) return preference.value ? 'Saved' : 'Not set';
    return preference.value || 'Not set';
  }

  function openGroup(group: SettingGroup) {
    path = [...path, group];
    notice = '';
    error = '';
  }

  function openField(field: SettingField) {
    const preference = preferences[field.key];
    if (!preference) return;
    if (preference.type === 'bool') {
      void savePreference(field, !preference.value);
      return;
    }
    draft = preference.type === 'string' && !(preference.password && preference.value === passwordMask)
      ? preference.value : '';
    selection = preference.type === 'int' ? preference.value : 0;
    editing = field;
    notice = '';
    error = '';
  }

  function goBack() {
    if (editing) editing = null;
    else path = path.slice(0, -1);
    error = '';
    notice = '';
  }

  async function savePreference(field: SettingField, value: boolean | number | string): Promise<boolean> {
    if (savingKey !== null) return false;
    savingKey = field.key;
    error = '';
    notice = '';
    try {
      const response = await fetch('/api/preferences', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key: field.key, value })
      });
      if (!response.ok) {
        const message = (await response.text()).trim();
        throw new Error(message || `Could not save ${field.label} (HTTP ${response.status})`);
      }
      const result: unknown = await response.json();
      if (!isPreference(result) || result.key !== field.key) throw new Error('Invalid save response');
      preferences = { ...preferences, [result.key]: result };
      notice = `${field.label} saved`;
      return true;
    } catch (cause) {
      error = cause instanceof Error ? cause.message : `Could not save ${field.label}`;
      return false;
    } finally {
      savingKey = null;
    }
  }

  async function saveDraft() {
    if (!editing) return;
    const field = editing;
    const preference = preferences[field.key];
    if (!preference || preference.type === 'bool') return;
    if (await savePreference(field, preference.type === 'int' ? selection : draft)) editing = null;
  }

  async function clearPassword() {
    if (!editing) return;
    if (await savePreference(editing, '')) editing = null;
  }

  function isDirty(preference: Preference | undefined, text: string, choice: number): boolean {
    if (preference?.type === 'int') return choice !== preference.value;
    if (preference?.type !== 'string') return false;
    if (preference.password && preference.value === passwordMask) return text.length > 0;
    return text !== preference.value;
  }
</script>

<section class="settings-shell" aria-label="Settings editor">
  <div class="settings-title">
    {#if path.length > 0 || editing}
      <button class="settings-back" onclick={goBack} aria-label="Back to previous settings menu">‹ Back</button>
    {/if}
    <div>
      <h2>{editing?.label ?? groupLabel(currentGroup, preferences)}</h2>
    </div>
  </div>

  {#if !connected && !loaded}
    <p class="settings-message">Connect to the player to edit settings.</p>
  {:else if loading && !loaded}
    <p class="settings-message">Loading settings…</p>
  {:else if !loaded}
    <div class="settings-message">
      <p>Settings could not be loaded.</p>
      <button onclick={() => void loadPreferences()} disabled={loading || !connected}>Retry</button>
    </div>
  {:else if editing}
    {@const preference = preferences[editing.key]}
    <div class="setting-editor">
      <p class="setting-location">Settings{#each path as group} / {groupLabel(group, preferences)}{/each}</p>
      {#if preference?.type === 'string'}
        <form onsubmit={(event) => { event.preventDefault(); void saveDraft(); }}>
          <label for="setting-value">{editing.label}</label>
          <input id="setting-value" type={preference.password ? 'password' : 'text'} value={draft}
            maxlength="512" autocomplete="off" autocapitalize="off" spellcheck="false"
            placeholder={preference.password && preference.value === passwordMask ? 'Saved password is hidden' : ''}
            oninput={(event) => draft = event.currentTarget.value} />
          {#if preference.password && preference.value === passwordMask}
            <p class="setting-help">Enter a new password, or clear the saved password below.</p>
            <button type="button" class="setting-secondary" onclick={() => void clearPassword()} disabled={savingKey !== null || !connected}>Clear saved password</button>
          {/if}
          {#if editing.help}<p class="setting-help">{editing.help}</p>{/if}
          <div class="setting-actions">
            <button type="button" class="setting-secondary" onclick={goBack}>Cancel</button>
            <button type="submit" disabled={!dirty || savingKey !== null || !connected}>{savingKey === editing.key ? 'Saving…' : 'Save'}</button>
          </div>
        </form>
      {:else if preference?.type === 'int' && editing.options}
        <form onsubmit={(event) => { event.preventDefault(); void saveDraft(); }}>
          <label for="setting-value">{editing.label}</label>
          <select id="setting-value" value={selection} onchange={(event) => selection = Number(event.currentTarget.value)}>
            {#each editing.options as option}
              <option value={option.value}>{option.label}</option>
            {/each}
          </select>
          {#if editing.help}<p class="setting-help">{editing.help}</p>{/if}
          <div class="setting-actions">
            <button type="button" class="setting-secondary" onclick={goBack}>Cancel</button>
            <button type="submit" disabled={!dirty || savingKey !== null || !connected}>{savingKey === editing.key ? 'Saving…' : 'Save'}</button>
          </div>
        </form>
      {/if}
    </div>
  {:else}
    {#if currentGroup.help}<p class="settings-help">{currentGroup.help}</p>{/if}
    <div class="settings-list">
      {#each currentGroup.items as item (item.kind === 'field' ? item.key : item.label)}
        {#if item.kind === 'group'}
          <button class="setting-row" onclick={() => openGroup(item)}>
            <span class="setting-label">{groupLabel(item, preferences)}</span><span class="setting-chevron" aria-hidden="true">›</span>
          </button>
        {:else}
          {@const preference = preferences[item.key]}
          <button class="setting-row" class:setting-toggle={preference?.type === 'bool'}
            role={preference?.type === 'bool' ? 'switch' : undefined}
            aria-checked={preference?.type === 'bool' ? preference.value : undefined}
            onclick={() => openField(item)} disabled={!preference || savingKey !== null || !connected}>
            <span class="setting-label">{item.label}</span>
            {#if preference?.type === 'bool'}
              <span class="setting-switch" class:on={preference.value} aria-hidden="true"></span>
            {:else}
              <span class="setting-value">{preference ? summary(preference, item) : 'Unavailable'}</span>
              <span class="setting-chevron" aria-hidden="true">›</span>
            {/if}
          </button>
        {/if}
      {/each}
    </div>
  {/if}

  {#if error}<p class="settings-feedback error" role="alert">{error}</p>{/if}
  {#if notice}<p class="settings-feedback" role="status">{notice}</p>{/if}
  {#if !connected && loaded}<p class="settings-feedback">Player disconnected. Editing is unavailable until it reconnects.</p>{/if}
</section>

window.ciLightsOta = (() => {
  const check = document.querySelector('#ota-check');
  const install = document.querySelector('#ota-install');
  const message = document.querySelector('#ota-message');
  const progress = document.querySelector('#ota-progress');
  const current = document.querySelector('#ota-current');
  const latest = document.querySelector('#ota-latest');
  const settingsForm = document.querySelector('#ota-automation-form');
  const details = document.querySelector('#ota-details');
  const fields = settingsForm ? {
    mode: document.querySelector('#ota-auto-mode'), interval: document.querySelector('#ota-auto-interval'),
    schedule: document.querySelector('#ota-auto-schedule'), time: document.querySelector('#ota-auto-time'),
    zone: document.querySelector('#ota-auto-timezone'), browser: document.querySelector('#ota-browser-timezone'),
    save: document.querySelector('#ota-auto-save'), note: document.querySelector('#ota-timezone-note'),
    message: document.querySelector('#ota-settings-message'), badge: document.querySelector('#ota-auto-badge'),
    state: document.querySelector('#ota-auto-state'), clock: document.querySelector('#ota-clock-time'),
    lastCheck: document.querySelector('#ota-last-check'), nextCheck: document.querySelector('#ota-next-check'),
    nextInstall: document.querySelector('#ota-next-install'), result: document.querySelector('#ota-last-result'),
    installed: document.querySelector('#ota-last-installed'), blocked: document.querySelector('#ota-blocked-version'),
  } : null;
  let status = null;
  let started = false;
  let polling = false;
  let waitingForReboot = false;
  let targetVersion = '';
  let rebootStarted = 0;
  let lastRefresh = 0;
  let zones = [];
  let zonesLoaded = false;
  let settingsLoaded = false;
  let settingsDirty = false;
  let settingsSaving = false;
  let timezoneAutomatic = false;
  let settingsMessage = '';
  let settingsError = false;

  function translate(source, values = {}) {
    let text = uiI18n.t(source);
    for (const [key, value] of Object.entries(values)) text = text.replace('{' + key + '}', value);
    return text;
  }

  function browserTimezone() {
    try { return Intl.DateTimeFormat().resolvedOptions().timeZone || ''; } catch { return ''; }
  }

  function showSettingsMessage(source, error = false) {
    settingsMessage = source;
    settingsError = error;
    fields.message.textContent = uiI18n.response(source);
    fields.message.dataset.error = String(error);
  }

  function renderSettingsControls() {
    if (!fields) return;
    const blocked = !settingsLoaded || settingsSaving || status?.busy || status?.boot_pending;
    const installEnabled = fields.mode.value === 'install';
    const daily = fields.schedule.value === 'daily';
    document.querySelector('#ota-schedule-field').hidden = !installEnabled;
    document.querySelector('#ota-time-field').hidden = !installEnabled || !daily;
    document.querySelector('#ota-auto-notice').hidden = !installEnabled;
    fields.mode.disabled = !!blocked;
    fields.interval.disabled = !!blocked || fields.mode.value === 'off';
    fields.schedule.disabled = !!blocked;
    fields.time.disabled = !!blocked;
    fields.time.required = installEnabled && daily;
    if (!fields.time.required && !/^([01]\d|2[0-3]):[0-5]\d$/.test(fields.time.value)) {
      fields.time.value = status?.automation?.install_time || '03:00';
    }
    fields.zone.disabled = !!blocked || !zonesLoaded;
    fields.zone.required = installEnabled && daily;
    fields.browser.disabled = !!blocked || !zonesLoaded || !zones.includes(browserTimezone());
    fields.save.disabled = !!blocked || !settingsLoaded || !settingsDirty;
    const zone = fields.zone.value;
    fields.note.textContent = zone ? translate(timezoneAutomatic ?
      'Automatisch vom Browser erkannt: {zone}. Sommer-/Winterzeit wird berücksichtigt.' :
      'Gewählte Zeitzone: {zone}. Sommer-/Winterzeit wird berücksichtigt.', {zone}) :
      uiI18n.t('Bitte eine Zeitzone auswählen.');
  }

  function formatLocal(value) {
    if (!value) return '–';
    // The device supplies its own local calendar time using its timezone database.
    // Format that wall-clock value without applying the browser's timezone again.
    try {
      return new Intl.DateTimeFormat(uiI18n.language === 'de' ? 'de-DE' : 'en-GB', {
        timeZone: 'UTC', dateStyle: 'short', timeStyle: 'medium',
      }).format(new Date(value + 'Z'));
    } catch { return value.replace('T', ' '); }
  }

  function renderAutomation() {
    const data = status?.automation;
    if (!fields || !data) return;
    if (zonesLoaded) settingsLoaded = true;
    if (!settingsDirty && !settingsSaving) {
      fields.mode.value = data.mode;
      fields.interval.value = String(data.interval_hours);
      fields.schedule.value = data.schedule;
      fields.time.value = data.install_time;
      if (zonesLoaded) {
        fields.zone.value = data.time_zone || '';
        timezoneAutomatic = !!data.timezone_auto;
        const detected = browserTimezone();
        if (!data.time_zone && zones.includes(detected)) {
          fields.zone.value = detected;
          timezoneAutomatic = true;
          settingsDirty = true;
          showSettingsMessage('Zeitzone erkannt. Zum Übernehmen die Einstellungen speichern.');
        }
      }
    }
    const modeLabels = {off: 'Aus', check: 'Nur prüfen', install: 'Automatisch'};
    fields.badge.textContent = uiI18n.t(modeLabels[data.mode] || 'Aus');
    fields.badge.dataset.active = String(data.mode !== 'off');
    fields.clock.textContent = data.clock_synced ? formatLocal(data.local_time) + ' · ' + (data.time_zone || 'UTC') :
      uiI18n.t('Internetzeit wird synchronisiert …');
    let state = 'Automatische Updates sind ausgeschaltet.';
    if (!data.storage_ok) state = 'Automatik angehalten: Einstellungen oder Verlauf konnten nicht gespeichert werden.';
    else if (data.mode !== 'off' && !data.running) state = 'Die Update-Automatik ist noch nicht gestartet.';
    else if (data.mode === 'check') state = 'Neue Updates werden angezeigt und manuell installiert.';
    else if (data.mode === 'install') {
      if (!data.clock_synced) state = 'Automatische Installation wartet auf eine gültige Internetzeit.';
      else if (data.version_blocked) state = 'Die verfügbare Version wird nach einem fehlgeschlagenen Start automatisch übersprungen.';
      else state = data.schedule === 'daily' ? 'Neue Updates warten auf die gespeicherte Installationszeit.' :
        'Neue Updates werden nach der Prüfung automatisch installiert.';
    }
    fields.state.textContent = uiI18n.t(state);
    fields.lastCheck.textContent = data.last_check_at ? formatLocal(data.last_check_local) :
      uiI18n.t(data.last_result === 'never' ? 'Noch nicht geprüft' : 'Zeitpunkt nicht verfügbar');
    fields.nextCheck.textContent = data.mode === 'off' || !data.running ? '–' : status.busy ? uiI18n.t('Wird ausgeführt …') :
      translate('In etwa {minutes} Minuten', {minutes: Math.max(1, Math.ceil(data.next_check_in_seconds / 60))});
    fields.nextInstall.textContent = data.mode !== 'install' ? '–' : data.schedule === 'immediate' ?
      uiI18n.t('Sobald ein Update verfügbar ist') : data.clock_synced ? formatLocal(data.next_install_local) :
        uiI18n.t('Wartet auf Internetzeit');
    const results = {never: 'Noch nicht nach Updates gesucht.', current: 'Die Firmware ist aktuell.',
      available: 'Neue Firmware ist verfügbar.', check_failed: 'Letzte Update-Prüfung fehlgeschlagen.',
      install_failed: 'Letzte Installation fehlgeschlagen.', installed: 'Update erfolgreich installiert.',
      rolled_back: 'Neue Firmware wurde zurückgerollt.'};
    fields.result.textContent = uiI18n.t(results[data.last_result] || results.never);
    fields.installed.textContent = data.last_installed_version ? data.last_installed_version +
      (data.last_install_local ? ' · ' + formatLocal(data.last_install_local) : '') : '–';
    fields.blocked.hidden = !data.blocked_version;
    fields.blocked.textContent = data.blocked_version ? translate(
      'Nach fehlgeschlagenem Start gesperrt: {version}. Eine neuere Version kann automatisch installiert werden; ein erneuter Versuch dieser Version ist manuell möglich.',
      {version: data.blocked_version}) : '';
    fields.message.textContent = uiI18n.response(settingsMessage);
    fields.message.dataset.error = String(settingsError);
    renderSettingsControls();
  }

  function renderTimezoneOptions() {
    const selected = fields.zone.value;
    fields.zone.replaceChildren(new Option(uiI18n.t('Bitte auswählen'), ''),
      ...zones.map(zone => new Option(zone === 'Europe/Berlin' ? uiI18n.t('Deutschland / Berlin') : zone, zone)));
    fields.zone.value = selected;
  }

  async function loadTimezones() {
    try {
      const data = await fetchJson('/api/ota/timezones', {cache: 'no-store'});
      if (!Array.isArray(data.zones) || !data.zones.every(zone => typeof zone === 'string')) throw new Error('Ungültige Zeitzonenliste.');
      zones = data.zones;
      renderTimezoneOptions();
      zonesLoaded = true;
      renderAutomation();
    } catch (error) { showSettingsMessage(error.message, true); }
  }

  async function saveAutomation(event) {
    event.preventDefault();
    if (settingsSaving || !settingsLoaded) return;
    const mode = fields.mode.value, schedule = fields.schedule.value, zone = fields.zone.value;
    const interval = Number(fields.interval.value), installTime = fields.time.value;
    if (!['off', 'check', 'install'].includes(mode) || ![1, 6, 12, 24].includes(interval) ||
        !['daily', 'immediate'].includes(schedule) || !/^([01]\d|2[0-3]):[0-5]\d$/.test(installTime) ||
        (zone && !zones.includes(zone)) || (mode === 'install' && schedule === 'daily' && !zone)) {
      showSettingsMessage('Ungültige Update-Einstellungen. Uhrzeit und Zeitzone prüfen.', true);
      return;
    }
    settingsSaving = true;
    renderSettingsControls();
    try {
      status = await fetchJson('/api/ota/settings', {method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({mode, interval_hours: interval, schedule, install_time: installTime,
          time_zone: zone, timezone_auto: timezoneAutomatic})});
      settingsDirty = false;
      showSettingsMessage('Update-Einstellungen gespeichert.');
    } catch (error) { showSettingsMessage(error.message, true); }
    finally { settingsSaving = false; render(); }
  }

  async function fetchJson(path, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch(path, {...options, signal: controller.signal});
      if (!response.ok) throw new Error(await response.text());
      return await response.json();
    } catch (error) {
      if (controller.signal.aborted) throw new Error('Die Update-Anfrage hat zu lange gedauert.');
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }

  function render() {
    if (!status) return;
    window.ciLightsSecurity?.update(status.security);
    current.textContent = status.current_version;
    latest.textContent = status.latest_version || '–';
    message.textContent = uiI18n.t(status.message);
    message.dataset.error = String(status.phase === 'error');
    check.disabled = status.busy || status.boot_pending || !status.configured || waitingForReboot;
    install.hidden = status.phase !== 'available';
    install.disabled = status.busy || status.boot_pending || waitingForReboot;
    progress.hidden = !['downloading', 'verifying', 'rebooting'].includes(status.phase);
    progress.value = status.total ? Math.round(status.bytes / status.total * 100) : 0;
    if (['verifying', 'rebooting'].includes(status.phase)) progress.value = 100;
    renderAutomation();
  }

  async function refresh() {
    if (polling) return;
    polling = true;
    try {
      status = await fetchJson('/api/ota', {cache: 'no-store'});
      lastRefresh = Date.now();
      if (status.phase === 'rebooting' && !waitingForReboot) {
        waitingForReboot = true;
        targetVersion = status.latest_version;
        rebootStarted = Date.now();
      }
      if (waitingForReboot && !status.boot_pending && status.phase !== 'rebooting') {
        if (status.current_version === targetVersion) {
          window.location.reload();
          return;
        }
        if (Date.now() - rebootStarted > 15000 && !status.busy) {
          waitingForReboot = false;
          status.phase = 'error';
          status.message = 'Die neue Firmware wurde nicht gestartet oder zurückgerollt.';
        }
      }
      render();
    } catch (error) {
      if (waitingForReboot) {
        if (Date.now() - rebootStarted > 120000) {
          waitingForReboot = false;
          status.busy = false;
          status.boot_pending = false;
          status.phase = 'error';
          status.message = 'Die Ampel ist noch nicht erreichbar. Verbindung prüfen und die Seite neu laden.';
          render();
        } else {
          message.textContent = uiI18n.t('Die Ampel startet neu. Verbindung wird wiederhergestellt.');
        }
      } else {
        message.textContent = uiI18n.t('Fehler: ') + uiI18n.response(error.message);
        message.dataset.error = 'true';
      }
    } finally {
      polling = false;
    }
  }

  async function request(path, body) {
    check.disabled = true;
    install.disabled = true;
    try {
      status = await fetchJson(path, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body),
      });
      render();
    } catch (error) {
      await refresh();
      message.textContent = uiI18n.t('Fehler: ') + uiI18n.response(error.message);
      message.dataset.error = 'true';
    }
  }

  function start() {
    if (started) { refresh(); return; }
    started = true;
    check.addEventListener('click', () => request('/api/ota/check', {}));
    install.addEventListener('click', () => {
      if (!status || status.phase !== 'available' || status.busy) return;
      if (confirm(uiI18n.t('Neue Firmware installieren? Die Ampel startet danach neu.'))) {
        request('/api/ota/install', {version: status.latest_version});
      }
    });
    uiI18n.onChange(() => {
      if (fields && zonesLoaded) renderTimezoneOptions();
      render();
    });
    if (fields) {
      settingsForm.addEventListener('submit', saveAutomation);
      for (const field of [fields.mode, fields.interval, fields.schedule, fields.time, fields.zone]) {
        field.addEventListener('input', () => {
          settingsDirty = true;
          if (field === fields.zone) timezoneAutomatic = false;
          showSettingsMessage('Änderungen noch nicht gespeichert.');
          renderSettingsControls();
        });
      }
      fields.browser.addEventListener('click', () => {
        const zone = browserTimezone();
        if (!zones.includes(zone)) return;
        fields.zone.value = zone;
        timezoneAutomatic = true;
        settingsDirty = true;
        showSettingsMessage('Zeitzone erkannt. Zum Übernehmen die Einstellungen speichern.');
        renderSettingsControls();
      });
      renderSettingsControls();
      loadTimezones();
    }
    refresh();
    setInterval(() => {
      if (status?.busy || status?.boot_pending || waitingForReboot) refresh();
      else if (!document.hidden && (details?.open || (status?.automation && status.automation.mode !== 'off')) &&
               Date.now() - lastRefresh >= 15000) refresh();
    }, 1500);
  }

  return {start, refresh};
})();

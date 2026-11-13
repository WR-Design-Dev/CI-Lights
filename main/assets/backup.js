(() => {
  const find = name => document.querySelector('#config-' + name);
  const importForm = find('import-form'), exportForm = find('export-form');
  if (!importForm && !exportForm) return;
  const file = find('import-file'), password = find('import-password'), review = find('import-review');
  const confirm = find('import-confirm'), apply = find('import-apply'), direct = find('import-direct'), summary = find('import-summary');
  const dialog = find('busy');
  let busy = false, restarting = false, validated = null;
  let summaryData = null, expiryTimer = null, progressText = null;
  const messages = new Map();
  function message(kind, text, error = false) {
    messages.set(kind, {text, error});
    const target = find(kind + '-message');
    target.textContent = uiI18n.response(text); target.dataset.error = String(error);
  }
  function controls() {
    for (const form of [importForm, exportForm]) if (form)
      for (const input of form.querySelectorAll('input,button')) input.disabled = busy || restarting;
    if (confirm) confirm.disabled = busy || restarting;
    if (apply) { apply.hidden = !validated; apply.disabled = busy || restarting || !validated || !confirm.checked; }
    if (direct) { direct.hidden = !!validated; direct.disabled = busy || restarting || !confirm.checked; }
  }
  function invalidate() {
    clearTimeout(expiryTimer); expiryTimer = null;
    validated = summaryData = null;
    review.hidden = true; confirm.checked = false; summary.textContent = '';
    message('import', ''); controls();
  }
  function expireReview() {
    if (restarting || !validated) return;
    if (busy) { expiryTimer = setTimeout(expireReview, 1000); return; }
    invalidate();
    message('import', 'Die geprüfte Konfiguration ist abgelaufen oder nicht mehr verfügbar. Datei erneut prüfen oder direkt übernehmen.', true);
  }
  function progress(title, detail = 'Das dauert ca. 60 Sekunden. Bitte warten.') {
    progressText = {title, detail};
    find('busy-title').textContent = uiI18n.t(title);
    find('busy-detail').textContent = uiI18n.t(detail);
    if (!dialog.open) dialog.showModal();
  }
  function stopProgress() { dialog.close(); progressText = null; }
  dialog.addEventListener('cancel', event => event.preventDefault());
  function validPassword(value) {
    const length = new TextEncoder().encode(value).length;
    return length >= 12 && length <= 128 && !/[\x00-\x1f\x7f]/.test(value);
  }
  async function request(action, body) {
    const deadline = Date.now() + 180000;
    async function exchange(route, options, timeout) {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), timeout);
      try {
        const response = await fetch(route, {...options, signal: controller.signal});
        if (!response.ok) throw new Error(await response.text());
        // Keep the timeout active until the complete response has arrived.
        return {status: response.status, data: await response.json()};
      } catch (error) {
        if (error instanceof SyntaxError) throw new Error('Ungültiger Konfigurationsauftrag.');
        if (controller.signal.aborted || error instanceof TypeError) {
          const lost = new Error('Verbindung zur Ampel unterbrochen. WLAN-Verbindung prüfen und erneut versuchen.');
          lost.connectionLost = true; throw lost;
        }
        throw error;
      } finally { clearTimeout(timer); }
    }
    const started = await exchange('/api/config/' + action, {method: 'POST',
      headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)}, 180000);
    if (started.status !== 202) return started.data;
    if (!/^[a-f0-9]{64}$/.test(started.data?.operation_id)) throw new Error('Ungültiger Konfigurationsauftrag.');
    const route = '/api/config/transfer?operation_id=' + started.data.operation_id;
    while (Date.now() < deadline) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      const remaining = deadline - Date.now();
      if (remaining <= 0) break;
      let response;
      try { response = await exchange(route, {method: 'GET', cache: 'no-store'}, Math.min(10000, remaining)); }
      catch (error) { if (error.connectionLost) continue; throw error; }
      const state = response.data;
      if (state?.done === true) {
        if (state.error) throw new Error(state.error);
        if (!state.result || typeof state.result !== 'object') throw new Error('Ungültiger Konfigurationsauftrag.');
        return state.result;
      }
      if (state?.pending !== true) throw new Error('Ungültiger Konfigurationsauftrag.');
    }
    throw new Error('Die Konfigurationsanfrage hat zu lange gedauert.');
  }
  function renderSummary() {
    if (!summaryData) return;
    const data = summaryData;
    summary.textContent = [uiI18n.t('Titel:') + ' ' + data.title,
      uiI18n.t('Gerätename:') + ' ' + (data.hostname || uiI18n.t('MAC-Standardname')),
      uiI18n.t('WLANs:') + ' ' + data.networks.join(', '),
      'Jenkins: ' + (data.jenkins_url || '–'), uiI18n.t('Job:') + ' ' + (data.job_path || '–'),
      uiI18n.t('Verwaltungsbenutzer:') + ' ' + data.admin_username].join('\n');
  }
  function chosenFile() {
    const chosen = file.files[0];
    if (!chosen || chosen.size < 1 || chosen.size > 160 * 1024) {
      message('import', 'Bitte eine Konfigurationsdatei bis 160 KiB auswählen.', true); return null;
    }
    if (!validPassword(password.value)) {
      message('import', 'Sicherungspasswort: mindestens 12 und höchstens 128 UTF-8-Bytes.', true); return null;
    }
    return chosen;
  }
  if (importForm) {
    file.addEventListener('change', invalidate); password.addEventListener('input', invalidate);
    confirm.addEventListener('change', controls);
    importForm.addEventListener('submit', async event => {
      event.preventDefault(); if (busy || restarting) return;
      invalidate();
      const chosen = chosenFile(); if (!chosen) return;
      busy = true; controls();
      message('import', 'Sicherung wird entschlüsselt und geprüft. Das kann etwas dauern.');
      progress('Sicherung wird entschlüsselt und geprüft.');
      try {
        const backup = JSON.parse(await chosen.text());
        summaryData = await request('validate', {password: password.value, backup});
        if (!/^[a-f0-9]{64}$/.test(summaryData.import_id) ||
            !Number.isInteger(summaryData.expires_in_seconds) || summaryData.expires_in_seconds < 1 || summaryData.expires_in_seconds > 600)
          throw new Error('Die geprüfte Konfiguration ist abgelaufen oder nicht mehr verfügbar. Datei erneut prüfen oder direkt übernehmen.');
        validated = {id: summaryData.import_id, password: password.value, file: chosen};
        expiryTimer = setTimeout(expireReview, summaryData.expires_in_seconds * 1000);
        renderSummary(); review.hidden = false;
        message('import', 'Sicherung geprüft. Für bis zu 10 Minuten ohne erneute Entschlüsselung übernehmbar. Zum Übernehmen die Ersetzung bestätigen.');
      } catch (error) { invalidate(); message('import', error instanceof SyntaxError ? 'Die Datei enthält kein gültiges JSON.' : error.message, true); }
      finally { stopProgress(); busy = false; controls(); }
    });
    async function importConfiguration(prepared) {
      if (busy || restarting || !confirm.checked || (prepared && !validated)) return;
      if (prepared && (validated.password !== password.value || validated.file !== file.files[0])) { invalidate(); return; }
      const chosen = prepared ? null : chosenFile(); if (!prepared && !chosen) return;
      busy = true; controls();
      message('import', 'Konfiguration wird übernommen. Stromversorgung angeschlossen lassen.');
      progress(prepared ? 'Konfiguration wird übernommen.' : 'Sicherung wird entschlüsselt und übernommen.',
        prepared ? 'Die bereits geprüfte Konfiguration wird gespeichert. Bitte warten.' : 'Das dauert ca. 60 Sekunden. Bitte warten.');
      try {
        const body = prepared ? {import_id: validated.id, confirmed: true} :
          {password: password.value, backup: JSON.parse(await chosen.text()), confirmed: true};
        await request('import', body);
        restarting = true; password.value = ''; clearTimeout(expiryTimer); validated = summaryData = null;
        message('import', 'Konfiguration übernommen. Die Ampel startet neu. Danach mit dem Verwaltungszugang aus der Sicherung anmelden.');
      } catch (error) {
        if (prepared) invalidate();
        message('import', error instanceof SyntaxError ? 'Die Datei enthält kein gültiges JSON.' : error.message, true);
      }
      finally { stopProgress(); busy = false; controls(); }
    }
    apply.addEventListener('click', () => importConfiguration(true));
    direct.addEventListener('click', () => importConfiguration(false));
  }
  if (exportForm) exportForm.addEventListener('submit', async event => {
    event.preventDefault(); if (busy || restarting) return;
    const value = find('export-password').value;
    if (!validPassword(value)) { message('export', 'Sicherungspasswort: mindestens 12 und höchstens 128 UTF-8-Bytes.', true); return; }
    if (value !== find('export-confirm').value) { message('export', 'Die Sicherungspasswörter stimmen nicht überein.', true); return; }
    busy = true; controls(); message('export', 'Verschlüsselte Sicherung wird erstellt. Das kann etwas dauern.');
    progress('Verschlüsselte Sicherung wird erstellt.');
    try {
      const backup = await request('export', {password: value, password_confirm: value});
      const blob = new Blob([JSON.stringify(backup)], {type: 'application/json'}), url = URL.createObjectURL(blob), link = document.createElement('a');
      link.href = url; link.download = 'CI-Lights-Konfiguration.json';
      document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000);
      find('export-password').value = find('export-confirm').value = '';
      message('export', 'Verschlüsselte Sicherung heruntergeladen. Sicherungspasswort separat aufbewahren.');
    } catch (error) { message('export', error.message, true); }
    finally { stopProgress(); busy = false; controls(); }
  });
  uiI18n.onChange(() => {
    for (const [kind, value] of messages) message(kind, value.text, value.error);
    renderSummary(); if (progressText) progress(progressText.title, progressText.detail);
  });
  controls();
})();

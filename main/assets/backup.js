(() => {
  const find = name => document.querySelector('#config-' + name);
  const importForm = find('import-form'), exportForm = find('export-form');
  if (!importForm && !exportForm) return;
  const file = find('import-file'), password = find('import-password'), review = find('import-review');
  const confirm = find('import-confirm'), apply = find('import-apply'), summary = find('import-summary');
  let busy = false, restarting = false, selected = null, validated = null;
  let summaryData = null;
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
    if (apply) apply.disabled = busy || restarting || !validated || !confirm.checked;
  }
  function invalidate() {
    selected = validated = summaryData = null;
    review.hidden = true; confirm.checked = false; summary.textContent = '';
    message('import', ''); controls();
  }
  function validPassword(value) {
    const length = new TextEncoder().encode(value).length;
    return length >= 12 && length <= 128 && !/[\x00-\x1f\x7f]/.test(value);
  }
  async function request(action, body) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 180000);
    try {
      const response = await fetch('/api/config/' + action, {method: 'POST',
        headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body), signal: controller.signal});
      if (!response.ok) throw new Error(await response.text());
      return response;
    } catch (error) {
      if (controller.signal.aborted) throw new Error('Die Konfigurationsanfrage hat zu lange gedauert.');
      throw error;
    } finally { clearTimeout(timer); }
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
  if (importForm) {
    file.addEventListener('change', invalidate); password.addEventListener('input', invalidate);
    confirm.addEventListener('change', controls);
    importForm.addEventListener('submit', async event => {
      event.preventDefault(); if (busy || restarting) return;
      invalidate();
      const chosen = file.files[0];
      if (!chosen || chosen.size < 1 || chosen.size > 160 * 1024) {
        message('import', 'Bitte eine Konfigurationsdatei bis 160 KiB auswählen.', true); return;
      }
      if (!validPassword(password.value)) { message('import', 'Sicherungspasswort: mindestens 12 und höchstens 128 UTF-8-Bytes.', true); return; }
      busy = true; controls();
      message('import', 'Sicherung wird entschlüsselt und geprüft. Das kann etwas dauern.');
      try {
        const backup = JSON.parse(await chosen.text());
        const response = await request('validate', {password: password.value, backup});
        summaryData = await response.json(); selected = backup;
        validated = {password: password.value, file: chosen};
        renderSummary(); review.hidden = false;
        message('import', 'Sicherung geprüft. Zum Übernehmen die Ersetzung bestätigen.');
      } catch (error) { invalidate(); message('import', error instanceof SyntaxError ? 'Die Datei enthält kein gültiges JSON.' : error.message, true); }
      finally { busy = false; controls(); }
    });
    apply.addEventListener('click', async () => {
      if (busy || restarting || !validated || !confirm.checked) return;
      if (validated.password !== password.value || validated.file !== file.files[0]) { invalidate(); return; }
      busy = true; controls();
      message('import', 'Konfiguration wird übernommen. Stromversorgung angeschlossen lassen.');
      try {
        await request('import', {password: password.value, backup: selected, confirmed: true});
        restarting = true; password.value = ''; selected = validated = null;
        message('import', 'Konfiguration übernommen. Die Ampel startet neu. Danach mit dem Verwaltungszugang aus der Sicherung anmelden.');
      } catch (error) { message('import', error.message, true); }
      finally { busy = false; controls(); }
    });
  }
  if (exportForm) exportForm.addEventListener('submit', async event => {
    event.preventDefault(); if (busy || restarting) return;
    const value = find('export-password').value;
    if (!validPassword(value)) { message('export', 'Sicherungspasswort: mindestens 12 und höchstens 128 UTF-8-Bytes.', true); return; }
    if (value !== find('export-confirm').value) { message('export', 'Die Sicherungspasswörter stimmen nicht überein.', true); return; }
    busy = true; controls(); message('export', 'Verschlüsselte Sicherung wird erstellt. Das kann etwas dauern.');
    try {
      const response = await request('export', {password: value, password_confirm: value});
      const blob = await response.blob(), url = URL.createObjectURL(blob), link = document.createElement('a');
      link.href = url; link.download = 'CI-Lights-Konfiguration.json';
      document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000);
      find('export-password').value = find('export-confirm').value = '';
      message('export', 'Verschlüsselte Sicherung heruntergeladen. Sicherungspasswort separat aufbewahren.');
    } catch (error) { message('export', error.message, true); }
    finally { busy = false; controls(); }
  });
  uiI18n.onChange(() => { for (const [kind, value] of messages) message(kind, value.text, value.error); renderSummary(); });
  controls();
})();

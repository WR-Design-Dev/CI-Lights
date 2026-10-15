window.ciLightsHostname = (() => {
  const parent = document.querySelector('.wifi-settings');
  if (!parent) return {start() {}};
  const form = document.createElement('form');
  form.className = 'hostname-settings';
  form.style.marginTop = '1.5rem';
  const title = document.createElement('h3');
  const label = document.createElement('label');
  const labelText = document.createElement('span');
  const input = document.createElement('input');
  input.name = 'hostname';
  input.type = 'text';
  input.maxLength = 32;
  input.pattern = '[A-Za-z0-9]([A-Za-z0-9\\-]{0,30}[A-Za-z0-9])?';
  input.autocomplete = 'off';
  input.spellcheck = false;
  input.placeholder = 'build-ampel';
  const hint = document.createElement('p');
  hint.className = 'hint';
  const active = document.createElement('p');
  active.className = 'hint';
  const address = document.createElement('a');
  const button = document.createElement('button');
  button.type = 'submit';
  const message = document.createElement('p');
  message.className = 'hint';
  message.setAttribute('role', 'status');
  label.append(labelText, input);
  form.append(title, label, hint, active, address, button, message);
  parent.append(form);
  let state;
  let started = false;
  let editing = false;
  let busy = false;
  let error = '';
  let revision = 0;
  const t = text => uiI18n.t(text);
  const needsRestart = () => state && (input.value.toLowerCase() !== state.configured || state.restart_required);

  function render() {
    title.textContent = t('Verwaltungsadresse (.local)');
    labelText.textContent = t('Gerätename (ohne .local)');
    hint.textContent = t('Bis zu 32 Zeichen: Buchstaben, Ziffern und Bindestriche; kein Bindestrich am Anfang oder Ende. Leer lassen für den MAC-Standardnamen. Nach Bestätigung wird die Ampel neu gestartet.');
    button.textContent = t('Gerätenamen speichern und neu starten');
    if (state) {
      active.textContent = t(state.restarting ? 'Adresse nach dem Neustart:' : 'Aktuelle Adresse:');
      address.textContent = `http://${state.restarting ? state.configured || state.default_name : state.active}.local`;
      address.href = address.textContent;
      if (!editing && !busy) input.value = state.configured;
    }
    button.disabled = busy || !needsRestart() || state.restarting;
    input.disabled = busy || !state || state.restarting;
    message.dataset.error = String(Boolean(error));
    message.textContent = error ? t('Fehler: ') + uiI18n.response(error) : !state ? t('Gerätename wird geladen.') :
      state.restarting ? t('Gespeichert. Die Ampel startet neu. Öffne danach die neue Adresse. Bei einem Namenskonflikt gilt der MAC-Standardname:') + ` ${state.default_name}.local` :
      [state.restart_required ? t('Neustart erforderlich. Zum Anwenden den Gerätenamen speichern und neu starten.') : '',
       state.fallback_pending ? t('Wunschname bereits vergeben. Der MAC-Standardname wird aktiviert.') :
       state.fallback ? t('Wunschname bereits vergeben. Der MAC-Standardname wird verwendet.') : ''].filter(Boolean).join(' ');
  }

  async function request(options = {}) {
    const response = await fetch('/api/hostname', {...options, cache: 'no-store', signal: AbortSignal.timeout(10000)});
    if (!response.ok) throw new Error(await response.text());
    return response.json();
  }

  async function refresh() {
    if (busy || state?.restarting) return;
    const requestedRevision = ++revision;
    try {
      const updated = await request();
      if (requestedRevision !== revision) return;
      state = updated; error = '';
    } catch (failure) {
      if (requestedRevision !== revision) return;
      error = failure.message;
    }
    render();
  }

  input.addEventListener('input', () => { editing = true; render(); });
  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (busy || !needsRestart() || state.restarting || !form.reportValidity()) return;
    const hostname = input.value;
    if (!window.confirm(t('Gerätenamen speichern und die Ampel neu starten? Danach ist sie unter dieser Adresse erreichbar:') +
      `\nhttp://${hostname.toLowerCase() || state.default_name}.local\n\n` +
      t('Bei einem Namenskonflikt verwendet sie den MAC-Standardnamen.'))) return;
    ++revision;
    busy = true;
    error = '';
    render();
    try {
      state = await request({method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({hostname, restart: true})});
      editing = false;
    } catch (failure) { error = failure.message; }
    finally { busy = false; render(); }
  });
  uiI18n.onChange(render);
  render();
  return {start() {
    if (!started) {
      started = true;
      setInterval(() => { if (!document.hidden && !busy && !editing) refresh(); }, 15000);
    }
    refresh();
  }};
})();

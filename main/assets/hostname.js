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

  function render() {
    title.textContent = t('Verwaltungsadresse (.local)');
    labelText.textContent = t('Gerätename (ohne .local)');
    hint.textContent = t('Bis zu 32 Zeichen: Buchstaben, Ziffern und Bindestriche; kein Bindestrich am Anfang oder Ende. Leer lassen für den MAC-Standardnamen. Änderungen gelten nach einem Neustart.');
    button.textContent = t('Gerätenamen speichern');
    button.disabled = busy || !state;
    input.disabled = busy || !state;
    if (state) {
      active.textContent = t('Aktuelle Adresse:');
      address.textContent = `http://${state.active}.local`;
      address.href = address.textContent;
      if (!editing && !busy) input.value = state.configured;
    }
    message.dataset.error = String(Boolean(error));
    message.textContent = error ? t('Fehler: ') + uiI18n.response(error) : !state ? t('Gerätename wird geladen.') :
      [state.restart_required ? t('Gespeichert. Der neue Name gilt nach dem nächsten Neustart.') : '',
       state.fallback_pending ? t('Wunschname bereits vergeben. Der MAC-Standardname wird aktiviert.') :
       state.fallback ? t('Wunschname bereits vergeben. Der MAC-Standardname wird verwendet.') : ''].filter(Boolean).join(' ');
  }

  async function request(options = {}) {
    const response = await fetch('/api/hostname', {...options, cache: 'no-store', signal: AbortSignal.timeout(10000)});
    if (!response.ok) throw new Error(await response.text());
    return response.json();
  }

  async function refresh() {
    if (busy) return;
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

  input.addEventListener('input', () => { editing = true; });
  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (busy || !state || !form.reportValidity()) return;
    const hostname = input.value;
    ++revision;
    busy = true;
    error = '';
    render();
    try {
      state = await request({method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({hostname})});
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

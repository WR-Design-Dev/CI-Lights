window.ciLightsOta = (() => {
  const check = document.querySelector('#ota-check');
  const install = document.querySelector('#ota-install');
  const message = document.querySelector('#ota-message');
  const progress = document.querySelector('#ota-progress');
  const current = document.querySelector('#ota-current');
  const latest = document.querySelector('#ota-latest');
  let status = null;
  let started = false;
  let polling = false;
  let waitingForReboot = false;
  let targetVersion = '';
  let rebootStarted = 0;

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
  }

  async function refresh() {
    if (polling) return;
    polling = true;
    try {
      status = await fetchJson('/api/ota', {cache: 'no-store'});
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
    uiI18n.onChange(render);
    refresh();
    setInterval(() => {
      if (status?.busy || status?.boot_pending || waitingForReboot) refresh();
    }, 1500);
  }

  return {start, refresh};
})();

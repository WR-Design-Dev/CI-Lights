function showSecurityStatus() {
  const updates = document.querySelector('#ota-details');
  if (!updates) return;
  const panel = document.createElement('section');
  panel.id = 'security-status';
  panel.className = 'device-group';
  panel.style.marginTop = '1rem';
  const title = document.createElement('h3');
  const status = document.createElement('p');
  const report = document.createElement('a');
  report.href = '/api/security';
  report.target = '_blank';
  report.rel = 'noopener';
  panel.append(title, status, report);
  updates.append(panel);
  let security;
  let loading = false;
  const translate = text => typeof uiI18n !== 'undefined' ? uiI18n.t(text) : text;
  function render() {
    title.textContent = translate('Sicherheitsstatus');
    report.textContent = translate('Sicherheitsbericht öffnen');
    if (!security) {
      status.textContent = translate(loading ? 'Sicherheitsstatus wird geladen.' : 'Sicherheitsstatus konnte nicht geladen werden.');
      return;
    }
    const active = translate('aktiv');
    const inactive = translate('inaktiv');
    status.textContent = `${translate('Profil')}: ${security.profile}. Secure Boot: ${security.secure_boot ? active : inactive}. ` +
      `${translate('Flash-Verschlüsselung')}: ${security.flash_encryption ? active : inactive}. ` +
      `${translate('NVS-Verschlüsselung')}: ${security.nvs_encryption ? active : inactive}. ` +
      `${translate('USB-/UART-Updates gesperrt')}: ${security.rom_download_disabled ? active : inactive}. ` +
      `${translate('Prüfung')}: ${security.ready ? translate('bestanden') : translate('fehlgeschlagen')}. ` +
      (security.signing_key === undefined ? '' : `K${security.signing_key}; ${translate('widerrufene Schlüssel')}: ${[0, 1, 2].filter(i => security.revoked_keys & (1 << i)).map(i => 'K' + i).join(', ') || '—'}. `) +
      (security.key_rotation === 'waiting_for_signed_fallback' ? translate('Schlüsselwechsel wartet auf ein weiteres bestätigtes Update mit dem neuen Schlüssel.') : '');
  }
  if (typeof uiI18n !== 'undefined') uiI18n.onChange(render);
  window.ciLightsSecurity = {update(data) { if (data) { security = data; render(); } }};
  render();
  // The administration page installs its authenticated fetch after login.
  // Opening the panel requests fresh status through that same fetch wrapper.
  updates.addEventListener('toggle', async () => {
    if (!updates.open || loading) return;
    loading = true;
    render();
    try {
      const response = await fetch('/api/security', {cache: 'no-store', signal: AbortSignal.timeout(10000)});
      if (!response.ok) throw new Error('security status');
      security = await response.json();
    } catch (_) { security = undefined; }
    finally { loading = false; render(); }
  });
}
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', showSecurityStatus, {once: true});
} else {
  showSecurityStatus();
}

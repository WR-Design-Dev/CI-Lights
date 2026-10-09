// Both setup and administration use the security advertised by the access points.
const ciLightsWifi = (() => {
  const names = new Map([[0, 'Offen'], [3, 'WPA2-Personal (PSK)'],
    [4, 'WPA/WPA2-Personal (PSK)'], [5, 'WPA2-Enterprise'], [6, 'WPA3-Personal (PSK)'],
    [7, 'WPA2/WPA3-Personal (PSK)'], [9, 'Enhanced Open (OWE)'],
    [14, 'WPA3-Enterprise'], [15, 'WPA2/WPA3-Enterprise']]);
  const kind = mode => [5, 14, 15].includes(mode) ? 'enterprise' :
    [3, 4, 6, 7].includes(mode) ? 'personal' : [0, 9].includes(mode) ? 'open' : 'unsupported';
  const t = value => uiI18n.t(value);
  function forForm(form) {
    const ssid = form.elements.wifi_ssid, mode = form.elements.wifi_auth;
    if (!ssid || !mode) return {setNetworks() {}, setMode() {}};
    const username = form.elements.wifi_username, password = form.elements.wifi_password;
    const hint = form.querySelector('[data-wifi-security]');
    const list = document.getElementById(ssid.getAttribute('list'));
    let networks = [], scanName = '', scanFailed = false, scanning = false;
    function render() {
      const network = networks.find(item => item.ssid === ssid.value);
      const modes = network?.authmodes || [];
      const kinds = [...new Set(modes.map(kind))];
      const detected = kinds.length === 1 ? kinds[0] : null;
      const effective = mode.value === 'auto' ? detected : mode.value;
      username.closest('label').hidden = effective === 'personal' || effective === 'open';
      username.required = effective === 'enterprise';
      password.required = effective === 'enterprise' || effective === 'personal';
      if (scanning && scanName === ssid.value) hint.textContent = t('Sicherheitsmodus wird geprüft ...');
      else if (modes.length) {
        hint.textContent = t('Erkannt:') + ' ' + modes.map(value => t(names.get(value) || 'Nicht unterstützter Sicherheitsmodus')).join(' / ');
        if (mode.value === 'auto' && detected === 'personal') hint.textContent += '. ' + t('Nur das WLAN-Passwort wird verwendet.');
        if (mode.value === 'auto' && detected === 'unsupported') hint.textContent += '. ' + t('Bitte ein anderes WLAN wählen.');
      } else hint.textContent = t(scanFailed ? 'Sicherheitsmodus konnte nicht geprüft werden. Beim Verbinden wird erneut gesucht.' :
        'Bei Automatik wird der Sicherheitsmodus beim Verbinden erneut erkannt.');
    }
    function renderList() {
      list.replaceChildren(...networks.map(network => {
        const label = network.ssid + ' — ' + network.authmodes.map(value => t(names.get(value) || 'Nicht unterstützter Sicherheitsmodus')).join(' / ');
        return new Option(label, network.ssid);
      }));
    }
    async function checkName() {
      render();
      if (!ssid.value || networks.some(item => item.ssid === ssid.value) || scanning) return;
      scanName = ssid.value;
      scanning = true;
      scanFailed = false;
      render();
      try {
        const response = await fetch('/api/wifi-networks?ssid=' + encodeURIComponent(scanName),
          {cache: 'no-store', signal: AbortSignal.timeout(10000)});
        if (!response.ok) throw new Error();
        const data = await response.json();
        networks = networks.filter(item => item.ssid !== scanName).concat(data.details || []);
        renderList();
      } catch {
        scanFailed = true;
      } finally {
        scanning = false;
        render();
        if (ssid.value !== scanName) checkName();
      }
    }
    mode.addEventListener('change', render);
    ssid.addEventListener('input', render);
    ssid.addEventListener('change', checkName);
    uiI18n.onChange(() => {renderList(); render();});
    render();
    return {
      setNetworks(details) {networks = details || []; scanFailed = false; renderList(); render();},
      setMode(value) {mode.value = value; render();},
    };
  }
  return {forForm};
})();

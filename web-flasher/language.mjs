import {messages} from './translations.mjs';

let language = 'en';
let page;
const listeners = new Set();

export function t(key, values = {}) {
  const template = messages[language][key] ?? messages.en[key];
  if (template === undefined) throw new Error(`Missing translation: ${key}`);
  return template.replace(/\{(\w+)\}/g, (_, name) => {
    const value = values[name];
    if (value instanceof Error || value?.message) {
      return value.translationKey ? t(value.translationKey, value.translationValues) : value.message;
    }
    return value === undefined ? `{${name}}` : String(value);
  });
}

export function localizedError(key, values = {}) {
  const error = new Error(t(key, values));
  error.translationKey = key;
  error.translationValues = values;
  return error;
}

export function onLanguageChange(callback) {
  listeners.add(callback);
  return () => listeners.delete(callback);
}

function render() {
  if (!page) return;
  const {document: doc, window: win} = page;
  doc.documentElement.lang = language;
  for (const element of doc.querySelectorAll('[data-i18n]')) {
    element.textContent = t(element.getAttribute('data-i18n'));
  }
  // HTML translations contain only trusted, checked-in text and links.
  // User input and device errors are always rendered as text by the installer.
  for (const element of doc.querySelectorAll('[data-i18n-html]')) {
    element.innerHTML = t(element.getAttribute('data-i18n-html'));
  }
  for (const attribute of ['aria-label', 'title', 'content', 'href']) {
    for (const element of doc.querySelectorAll(`[data-i18n-${attribute}]`)) {
      element.setAttribute(attribute, t(element.getAttribute(`data-i18n-${attribute}`)));
    }
  }
  for (const button of doc.querySelectorAll('[data-language]')) {
    button.setAttribute('aria-pressed', String(button.getAttribute('data-language') === language));
    button.disabled = false;
  }
  for (const link of doc.querySelectorAll('a[href]')) {
    const url = new URL(link.getAttribute('href'), win.location.href);
    if (url.origin !== win.location.origin || !url.pathname.endsWith('.html')) continue;
    url.searchParams.set('lang', language);
    link.setAttribute('href', url.pathname + url.search + url.hash);
  }
}

export function setLanguage(value) {
  if (value !== 'en' && value !== 'de') return;
  language = value;
  if (page) {
    const url = new URL(page.window.location.href);
    url.searchParams.set('lang', language);
    page.window.history.replaceState(null, '', url.href);
    render();
  }
  for (const listener of listeners) listener();
}

export function initLanguage(doc = document, win = window) {
  // English is the default even for browsers whose preferred language is German.
  language = new URL(win.location.href).searchParams.get('lang') === 'de' ? 'de' : 'en';
  page = {document: doc, window: win};
  for (const button of doc.querySelectorAll('[data-language]')) {
    button.addEventListener('click', () => setLanguage(button.getAttribute('data-language')));
  }
  render();
}

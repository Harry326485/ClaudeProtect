const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');

// Minimal DOM for testing state and message routing without a WebView session.
const elements = new Map();
const isMac = true;
class Element {
  constructor(tagName = 'div') {
    this.tagName = tagName.toUpperCase();
    this.children = [];
    this.options = [];
    this.dataset = {};
    this.attributes = new Map();
    this.value = '';
    this.disabled = false;
  }
  set id(value) { this.elementId = value; elements.set(value, this); }
  get id() { return this.elementId; }
  append(...children) { this.children.push(...children); }
  add(option) { this.options.push(option); }
  addEventListener() {}
  setAttribute(key, value) { this.attributes.set(key, value); }
  removeAttribute(key) { this.attributes.delete(key); }
  querySelector(selector) {
    const descendants = this.children.flatMap(child => [child, ...child.descendants()]);
    return descendants.find(child => {
      if (selector.startsWith('.')) return child.className === selector.slice(1);
      const action = selector.match(/^\[data-action=(.+)\]$/);
      return action && child.dataset.action === action[1];
    });
  }
  descendants() { return this.children.flatMap(child => [child, ...child.descendants()]); }
  querySelectorAll() { return []; }
}

const root = path.resolve(__dirname, '../..');
for (const match of fs.readFileSync(path.join(root, 'macos/ui/index.html'), 'utf8').matchAll(/id="([^"]+)"/g)) {
  const element = new Element();
  element.id = match[1];
}
elements.get('summary-verification').previousElementSibling = new Element('dt');
const logos = {'.logo-stopped': new Element(), '.logo-running': new Element()};
const messages = [];
let receive;
const context = vm.createContext({
  document: {
    getElementById: id => elements.get(id),
    createElement: tagName => new Element(tagName),
    createTextNode: text => Object.assign(new Element(), { textContent: text }),
    querySelector: selector => logos[selector],
    querySelectorAll: () => [],
    addEventListener() {},
    body: { dataset: {} }
  },
  Option: function(text, value) { return Object.assign(new Element('option'), { textContent: text, value }); },
  window: {
    CP_PLATFORM: isMac ? 'macos' : undefined,
    chrome: { webview: {
      postMessage: message => messages.push(message),
      addEventListener: (event, callback) => { receive = callback; }
    } },
    scrollTo() {}
  },
  setTimeout: () => 1,
  clearTimeout() {}
});
const source = fs.readFileSync(path.join(root, 'macos/ui/app.js'), 'utf8').replace(/initialize\(\);\s*$/, '');
vm.runInContext(source, context);
async function checkAction(button, action, application) {
  const pending = button.onclick();
  const message = messages.at(-1);
  assert.equal(message.action, action);
  assert.equal(message.application, application);
  await receive({ data: { type: 'response', id: message.id, ok: true, data: {} } });
  await pending;
}

if (isMac) {
  vm.runInContext(`
    buildForm([]);
    applyValues({executable:'/Applications/Claude.app', extraarguments:'--disable-gpu'});
    buildSessions(sessionApplications({active:false, timezone:'UTC',locale:'en-US'}));
    updateStatus({active:true, busy:false, state:'running', processCount:7, timezone:'UTC',locale:'en-US'});
  `, context);
  assert.equal(elements.get('field-executable').value, '/Applications/Claude.app');
  assert.equal(elements.get('field-extraarguments').value, '--disable-gpu');
  assert.equal(elements.has('field-scienceexecutable'), false);
  assert.equal(elements.has('session-science'), false);
  const keys = Object.keys(vm.runInContext('values()', context));
  assert(!keys.includes('desktopexecutable'));
  assert(!keys.includes('desktoparguments'));
  assert.equal(logos['.logo-running'].hidden, false);
  const desktop = elements.get('session-desktop');
  assert.equal(desktop.querySelector('[data-action=start]').disabled, true);
  assert.equal(desktop.querySelector('[data-action=stop]').disabled, false);
  (async () => {
    await checkAction(desktop.querySelector('[data-action=stop]'), 'stop', 'desktop');
    vm.runInContext("updateStatus({active:false, busy:false, state:'stopped',timezone:'UTC',locale:'en-US'});", context);
    assert.equal(desktop.querySelector('[data-action=start]').disabled, false);
    assert.equal(desktop.querySelector('[data-action=stop]').disabled, true);
    assert.equal(logos['.logo-running'].hidden, true);
    await checkAction(desktop.querySelector('[data-action=start]'), 'start', 'desktop');
    console.log('macOS single-session settings, controls, state and icon routing passed.');
  })().catch(error => { console.error(error); process.exitCode = 1; });
}

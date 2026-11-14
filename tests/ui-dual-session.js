const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');

// Minimal DOM for testing state and message routing without a WebView session.
const elements = new Map();
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

const root = path.resolve(__dirname, '..');
for (const match of fs.readFileSync(path.join(root, 'ui/index.html'), 'utf8').matchAll(/id="([^"]+)"/g)) {
  const element = new Element();
  element.id = match[1];
}
const messages = [];
let receive;
const context = vm.createContext({
  document: {
    getElementById: id => elements.get(id),
    createElement: tagName => new Element(tagName),
    createTextNode: text => Object.assign(new Element(), { textContent: text }),
    querySelectorAll: () => [],
    addEventListener() {},
    body: { dataset: {} }
  },
  Option: function(text, value) { return Object.assign(new Element('option'), { textContent: text, value }); },
  window: {
    chrome: { webview: {
      postMessage: message => messages.push(message),
      addEventListener: (event, callback) => { receive = callback; }
    } },
    scrollTo() {}
  },
  setTimeout: () => 1,
  clearTimeout() {}
});
const source = fs.readFileSync(path.join(root, 'ui/app.js'), 'utf8').replace(/initialize\(\);\s*$/, '');
vm.runInContext(source, context);
vm.runInContext(`
  buildForm([]);
  applyValues({ application: 'science', executable: 'science-legacy.exe' });
  buildSessions([{id:'desktop',name:'Claude Desktop'}, {id:'science',name:'Claude Science'}]);
  updateStatus({active:true, busy:false, applications:[
    {id:'desktop', active:true, processCount:7, timezone:'UTC', locale:'en-US'},
    {id:'science', active:false, processCount:0, timezone:'UTC', locale:'en-US'}
  ]});
`, context);
assert.equal(elements.get('field-scienceexecutable').value, 'science-legacy.exe');
assert.equal(elements.get('field-desktopexecutable').value, '');
const desktop = elements.get('session-desktop');
const science = elements.get('session-science');
assert.equal(desktop.querySelector('[data-action=start]').disabled, true);
assert.equal(desktop.querySelector('[data-action=stop]').disabled, false);
assert.equal(science.querySelector('[data-action=start]').disabled, false);

async function checkAction(button, action, application) {
  const pending = button.onclick();
  const message = messages.at(-1);
  assert.equal(message.action, action);
  assert.equal(message.application, application);
  await receive({ data: { type: 'response', id: message.id, ok: true, data: {} } });
  await pending;
}

(async () => {
  await checkAction(science.querySelector('[data-action=start]'), 'start', 'science');
  vm.runInContext('status.busy = true;', context);
  await checkAction(desktop.querySelector('[data-action=stop]'), 'stop', 'desktop');
  console.log('Dual application controls, legacy settings and independent message routing passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });

'use strict';

const fields = [
  { key: 'timezone', ini: 'Timezone', page: 'general', label: '时区', type: 'timezone', fallback: 'Pacific Standard Time', help: '按标准时 UTC 偏移排列，夏令时单独标注；IANA 名称自动匹配。' },
  { key: 'locale', ini: 'Locale', page: 'general', label: '语言与区域', fallback: 'en-US', options: [
    ['en-US', '英语（美国） · en-US'],
    ['en-GB', '英语（英国） · en-GB'],
    ['en-SG', '英语（新加坡） · en-SG'],
    ['en-AU', '英语（澳大利亚） · en-AU'],
    ['en-CA', '英语（加拿大） · en-CA'],
    ['en-NZ', '英语（新西兰） · en-NZ'],
    ['zh-CN', '简体中文（中国） · zh-CN'],
    ['zh-TW', '繁体中文（台湾） · zh-TW'],
    ['zh-HK', '繁体中文（香港） · zh-HK'],
    ['ja-JP', '日语（日本） · ja-JP'],
    ['ko-KR', '韩语（韩国） · ko-KR'],
    ['de-DE', '德语（德国） · de-DE'],
    ['fr-FR', '法语（法国） · fr-FR'],
    ['it-IT', '意大利语（意大利） · it-IT'],
    ['es-ES', '西班牙语（西班牙） · es-ES'],
    ['pt-PT', '葡萄牙语（葡萄牙） · pt-PT'],
    ['pt-BR', '葡萄牙语（巴西） · pt-BR'],
    ['nl-NL', '荷兰语（荷兰） · nl-NL'],
    ['sv-SE', '瑞典语（瑞典） · sv-SE'],
    ['fi-FI', '芬兰语（芬兰） · fi-FI'],
    ['da-DK', '丹麦语（丹麦） · da-DK'],
    ['nb-NO', '挪威语（挪威） · nb-NO'],
    ['pl-PL', '波兰语（波兰） · pl-PL'],
    ['el-GR', '希腊语（希腊） · el-GR'],
    ['th-TH', '泰语（泰国） · th-TH'],
    ['vi-VN', '越南语（越南） · vi-VN'],
    ['id-ID', '印度尼西亚语 · id-ID']
  ], help: 'BCP-47 区域代码，用于区域、默认代码页和首选语言，Desktop 自动传入 --lang 参数，Science 通过进程内语言环境传递。从下拉列表选择常用语言与区域。' },
  { key: 'verifyinjection', ini: 'VerifyInjection', page: 'protection', label: '运行中注入验证', fallback: 'strict', options: [['strict', '严格 · 检测失败时停止会话（推荐）'], ['warn', '警告 · 仅记录日志'], ['off', '关闭运行中验证']] },
  { key: 'verifygraceseconds', ini: 'VerifyGraceSeconds', page: 'protection', label: '注入等待时间', type: 'number', fallback: '8', min: 1, max: 120, help: '新进程创建后，等待指定秒数再检查注入证据，为初始化留出时间。允许 1–120 秒，默认 8 秒。' },
  { key: 'preflight', ini: 'Preflight', page: 'protection', label: '启动前环境预检', fallback: 'auto', options: [['auto', '自动 · 有探针时执行（默认）'], ['require', '必须 · 探针缺失或检查失败时拒绝启动'], ['off', '关闭启动预检']] },
  { key: 'ansicodepage', ini: 'AnsiCodePage', page: 'advanced', label: 'ANSI 代码页', type: 'number', fallback: '0', min: 0, max: 65535, help: '0 按语言与区域自动推导。用于传统 Windows 文本编码，例如美式英语 1252、简体中文 936。允许 0–65535。' },
  { key: 'oemcodepage', ini: 'OemCodePage', page: 'advanced', label: 'OEM 代码页', type: 'number', fallback: '0', min: 0, max: 65535, help: '0 自动推导。用于传统控制台编码，例如美式英语 437。允许 0–65535。' },
  { key: 'fontcharset', ini: 'FontCharset', page: 'advanced', label: '字体字符集', type: 'number', fallback: '0', min: 0, max: 255, help: '0 按代码页自动推导。使用 Windows 字符集编号，例如简体中文 134。允许 0–255。' },
  { key: 'registryredirectionmode', ini: 'RegistryRedirectionMode', page: 'advanced', label: '注册表查询重定向', fallback: '2', options: [['0', '0 · 基础代码页与语言查询'], ['1', '1 · NLS 语言与代码页查询'], ['2', '2 · 扩展区域与首选语言（推荐）']], help: '在受监管进程内覆盖相关注册表查询。模式 0 覆盖基础 ACP、OEMCP 和默认语言，模式 1 调整 NLS 查询项，模式 2 扩展到用户区域和首选语言。' },
  { key: 'hookuilanguagemode', ini: 'HookUILanguageMode', page: 'advanced', label: '界面语言兼容模式', fallback: '2', options: [['0', '0 · 关闭界面语言专用钩子'], ['1', '1 · Win32 界面语言'], ['2', '2 · MUI、首选语言与 WinRT（推荐）']], help: '模式 1 覆盖 Win32 默认 UI 语言。模式 2 进一步覆盖 native UI、虚拟 MUI、线程首选语言和 WinRT 语言列表。Claude 自身保存的界面语言仍可能优先。' },
  { key: 'desktopexecutable', ini: 'DesktopExecutable', page: 'launch', label: 'Claude Desktop 启动器', browse: 'file', placeholder: '留空，自动发现 Desktop', help: '支持常规安装和 Microsoft Store 安装。' },
  { key: 'scienceexecutable', ini: 'ScienceExecutable', page: 'launch', label: 'Claude Science 启动器', browse: 'file', placeholder: '留空，自动发现 Science', help: '选择 ClaudeScience 安装目录中的 claude-science.exe。Science 使用默认桌面启动交接。' },
  { key: 'desktoparguments', ini: 'DesktopArguments', page: 'launch', label: 'Desktop 附加启动参数', placeholder: '例如 --lang=ja-JP', help: '传给 Desktop，含空格的路径参数需要加引号。Science 桌面启动使用默认参数。' }
];

const $ = id => document.getElementById(id);
const pending = new Map();
let nextId = 0;
let baseline = {};
let dirty = false;
let saving = false;
let status = {};
let toastTimer;
let dialogQueue = Promise.resolve();
let closing = false;

function request(action, payload = {}) {
  return new Promise((resolve, reject) => {
    const id = ++nextId;
    const timeout = action === 'browse' ? null : setTimeout(() => {
      pending.delete(id);
      reject(new Error('操作未完成，请重试或重新打开控制面板。'));
    }, 15000);
    pending.set(id, { resolve, reject, timeout });
    window.chrome.webview.postMessage({ id, action, ...payload });
  });
}

function toast(message) {
  clearTimeout(toastTimer);
  $('toast').textContent = message;
  $('toast').hidden = false;
  toastTimer = setTimeout(() => { $('toast').hidden = true; }, 3500);
}

function dialog(title, message, choices = [['确定', 'ok', true]]) {
  const result = dialogQueue.then(() => new Promise(resolve => {
    const element = $('message-dialog');
    if (element.open) element.close();
    $('dialog-title').textContent = title;
    $('dialog-message').textContent = message;
    $('dialog-actions').replaceChildren();
    for (const [label, value, primary] of choices) {
      const button = document.createElement('button');
      button.className = `button${primary ? ' primary' : ''}`;
      button.textContent = label;
      button.onclick = () => { element.close(); resolve(value); };
      $('dialog-actions').append(button);
    }
    element.oncancel = event => { event.preventDefault(); element.close(); resolve('cancel'); };
    element.showModal();
  }));
  dialogQueue = result;
  return result;
}

function showView(view) {
  document.body.dataset.view = view;
  for (const element of document.querySelectorAll('.view')) element.hidden = element.id !== view;
  for (const button of document.querySelectorAll('.nav-button')) {
    if (button.dataset.view === view) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
  $('settings-footer').hidden = view !== 'settings';
  window.scrollTo(0, 0);
}

function showPage(page) {
  for (const button of document.querySelectorAll('[role=tab]')) {
    const selected = button.dataset.page === page;
    button.setAttribute('aria-selected', String(selected));
    button.tabIndex = selected ? 0 : -1;
  }
  for (const element of document.querySelectorAll('.settings-page')) element.hidden = element.id !== `page-${page}`;
  window.scrollTo(0, 0);
}

function values() {
  return Object.fromEntries(fields.map(field => [field.key, $(`field-${field.key}`).value]));
}

function updateDirty() {
  const current = values();
  const changed = fields.some(field => current[field.key] !== baseline[field.key]);
  if (changed !== dirty) {
    dirty = changed;
    window.chrome.webview.postMessage({ action: 'dirty', dirty });
  }
  $('save').disabled = !dirty || saving;
  $('save-state').textContent = dirty ? '有未保存的更改 · 下次启动会话时生效' : '更改将在下次启动会话时生效';
}

function buildForm(timezones) {
  const notes = {
    advanced: '通常保留默认值即可。代码页与字体字符集为 0 时，会随语言与区域自动推导。'
  };
  for (const [page, text] of Object.entries(notes)) {
    const note = document.createElement('p');
    note.className = 'page-note';
    note.textContent = text;
    $(`page-${page}`).append(note);
  }
  for (const field of fields) {
    const group = document.createElement('div');
    group.className = 'field';
    const label = document.createElement('label');
    label.htmlFor = `field-${field.key}`;
    label.append(document.createTextNode(field.label));
    const key = document.createElement('span');
    key.className = 'field-key';
    key.textContent = field.ini;
    label.append(key);
    const help = document.createElement('p');
    help.id = `help-${field.key}`;
    help.className = 'field-help';
    help.textContent = field.help || '';
    help.hidden = !field.help;
    const row = document.createElement('div');
    row.className = `input-row${field.browse || field.key === 'desktoparguments' ? ' wide' : ''}`;
    const options = field.type === 'timezone' ? timezones.map(zone => [zone.id, zone.label]) : field.options;
    const input = document.createElement(options ? 'select' : 'input');
    input.id = `field-${field.key}`;
    input.name = field.key;
    if (field.help) input.setAttribute('aria-describedby', help.id);
    if (options) {
      for (const [value, text] of options) input.add(new Option(text, value));
    } else {
      input.type = 'text';
      input.autocomplete = 'off';
      input.spellcheck = false;
      input.maxLength = field.maxLength || 32767;
      input.placeholder = field.placeholder || '';
      if (field.type === 'number') input.inputMode = 'numeric';
    }
    input.addEventListener('input', () => {
      input.removeAttribute('aria-invalid');
      $(`error-${field.key}`).textContent = '';
      updateDirty();
    });
    row.append(input);
    if (field.browse) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'button';
      button.textContent = '浏览…';
      button.setAttribute('aria-label', `浏览${field.label}`);
      button.onclick = async () => {
        try {
          const result = await request('browse', { key: field.key });
          if (result.path) { input.value = result.path; updateDirty(); }
        } catch (error) { await dialog('无法选择路径', error.message); }
      };
      row.append(button);
    }
    const error = document.createElement('p');
    error.id = `error-${field.key}`;
    error.className = 'field-error';
    error.setAttribute('role', 'alert');
    group.append(label, help, row, error);
    $(`page-${field.page}`).append(group);
  }
}

function applyValues(loaded) {
  const selected = (loaded.application || 'desktop').toLowerCase();
  loaded = { ...loaded };
  loaded.desktopexecutable ??= selected === 'desktop' ? loaded.executable || '' : '';
  loaded.scienceexecutable ??= selected === 'science' ? loaded.executable || '' : '';
  loaded.desktoparguments ??= selected === 'desktop' ? loaded.extraarguments || '' : '';
  baseline = {};
  for (const field of fields) {
    let value = Object.hasOwn(loaded, field.key) ? loaded[field.key] : field.fallback || '';
    if (field.key === 'verifyinjection') value = value === '1' ? 'strict' : value === '0' ? 'off' : value;
    if (field.options) {
      const match = field.options.find(([code]) => code.toLowerCase() === value.toLowerCase());
      if (match) value = match[0];
    }
    const input = $(`field-${field.key}`);
    if (input.tagName === 'SELECT') input.querySelectorAll('[data-legacy]').forEach(option => option.remove());
    if (input.tagName === 'SELECT' && !Array.from(input.options).some(option => option.value === value)) {
      const legacy = new Option(value ? `当前值 · ${value}（请选择列表中的选项替换）` : '未设置 · 使用默认值', value);
      legacy.disabled = true;
      legacy.dataset.legacy = 'true';
      input.add(legacy);
    }
    input.value = value;
    input.removeAttribute('aria-invalid');
    $(`error-${field.key}`).textContent = '';
    baseline[field.key] = value;
  }
  updateDirty();
}

function updateStatus(next) {
  status = next;
  const busy = Boolean(next.busy);
  $('status-pill').textContent = busy ? '处理中' : next.active ? '受监管运行中' : '待机';
  $('status-pill').className = `status-pill${busy ? ' busy' : next.active ? ' active' : ''}`;
  $('session-title').textContent = busy ? next.busyLabel : next.active ? 'Claude 正在受监管运行' : next.detail ? '需要检查配置' : '准备启动';
  $('session-description').textContent = busy ? '正在检查环境，请稍候…' : next.active ? `Job 内共有 ${next.processCount} 个进程。` : '分别启动 Desktop 和 Science，两个会话独立运行。';
  $('summary-timezone').textContent = next.timezone || '未设置';
  $('summary-locale').textContent = next.locale || '未设置';
  $('summary-verification').textContent = ({ strict: '严格验证', warn: '仅记录警告', off: '已关闭' })[next.verify] || '未设置';
  $('status-detail').textContent = next.detail || '';
  $('status-detail').hidden = !next.detail;
  $('selftest').disabled = busy;
  for (const app of next.applications || []) {
    const row = $(`session-${app.id}`);
    if (!row) continue;
    row.querySelector('.app-state').textContent = app.busy ? '正在预检并启动…' : app.active ? `受监管运行 · ${app.processCount} 个进程` : '未启动';
    row.querySelector('.app-environment').textContent = `${app.timezone} · ${app.locale}`;
    row.querySelector('[data-action=start]').disabled = busy || app.active;
    row.querySelector('[data-action=stop]').disabled = !app.active;
  }
}

function invalidField(field, message) {
  showView('settings');
  showPage(field.page);
  const input = $(`field-${field.key}`);
  input.setAttribute('aria-invalid', 'true');
  $(`error-${field.key}`).textContent = message;
  input.focus();
  input.scrollIntoView({ block: 'center' });
}

async function save() {
  if (saving) return false;
  const current = values();
  for (const field of fields) {
    if (field.type === 'number' && current[field.key] !== '' &&
        (!/^\d+$/.test(current[field.key]) || Number(current[field.key]) < field.min || Number(current[field.key]) > field.max)) {
      invalidField(field, `请输入 ${field.min}–${field.max} 之间的整数。`);
      return false;
    }
  }
  saving = true;
  $('save').disabled = true;
  try {
    const result = await request('save', { values: current });
    applyValues(result.values);
    toast('设置已保存，将在下次启动会话时生效');
    return true;
  } catch (error) {
    const field = fields.find(item => error.message.includes(item.ini));
    if (field) invalidField(field, error.message);
    await dialog('无法保存设置', error.message);
    return false;
  } finally {
    saving = false;
    updateDirty();
  }
}

async function run(action, application) {
  if (action !== 'stop' && status.busy) return;
  if (dirty && action !== 'stop') {
    const choice = await dialog('有未保存的设置', '是否先保存设置？使用已保存配置会保留当前表单中的更改。',
      [['取消', 'cancel'], ['使用已保存配置', 'previous'], ['保存并继续', 'save', true]]);
    if (choice === 'cancel' || (choice === 'save' && !await save())) return;
  }
  try { await request(action, { application }); } catch (error) { await dialog('操作失败', error.message); }
}

window.chrome.webview.addEventListener('message', async event => {
  const message = event.data;
  if (message.type === 'response') {
    const item = pending.get(message.id);
    if (!item) return;
    clearTimeout(item.timeout);
    pending.delete(message.id);
    if (message.ok) item.resolve(message.data);
    else item.reject(new Error(message.error));
  } else if (message.type === 'status') updateStatus(message.data);
  else if (message.type === 'navigate') {
    if (message.data.page) showPage(message.data.page);
    showView(message.data.view);
  } else if (message.type === 'notice') await dialog(message.error ? '操作失败' : '环境自检', message.text);
  else if (message.type === 'closeRequested') {
    if (closing) return;
    closing = true;
    const choice = await dialog('有未保存的设置', '关闭窗口后，ClaudeProtect 会继续在托盘运行。',
      [['继续编辑', 'cancel'], ['放弃更改并关闭', 'discard'], ['保存并关闭', 'save', true]]);
    closing = false;
    if (choice === 'discard' || (choice === 'save' && await save())) await request('close');
  }
});

document.querySelectorAll('[data-view]').forEach(button => button.addEventListener('click', () => showView(button.dataset.view)));
document.querySelectorAll('[data-page]').forEach(button => button.addEventListener('click', () => showPage(button.dataset.page)));
$('settings-tabs').addEventListener('keydown', event => {
  const tabs = Array.from(document.querySelectorAll('[role=tab]'));
  const index = tabs.indexOf(document.activeElement);
  if (index < 0 || !['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
  event.preventDefault();
  const next = event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 : (index + (event.key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length;
  showPage(tabs[next].dataset.page);
  tabs[next].focus();
});
$('settings-form').addEventListener('submit', event => { event.preventDefault(); save(); });
$('save').onclick = save;
$('reload').onclick = async () => {
  if (dirty && await dialog('重新加载设置', '这会放弃所有未保存的更改，并读取磁盘上的最新配置。', [['取消', 'cancel'], ['重新加载', 'reload', true]]) !== 'reload') return;
  try { applyValues((await request('load')).values); toast('已重新加载设置'); }
  catch (error) { await dialog('无法加载设置', error.message); }
};

$('selftest').onclick = () => run('selftest');

document.addEventListener('keydown', event => {
  if (event.ctrlKey && event.key.toLowerCase() === 's') { event.preventDefault(); if (dirty) save(); }
});

function buildSessions(applications) {
  for (const app of applications) {
    const row = document.createElement('article');
    row.className = 'application-session';
    row.id = `session-${app.id}`;
    const name = document.createElement('h3');
    name.textContent = app.name;
    const state = document.createElement('p');
    state.className = 'app-state';
    state.setAttribute('role', 'status');
    const environment = document.createElement('p');
    environment.className = 'app-environment';
    const actions = document.createElement('div');
    actions.className = 'session-actions';
    for (const [action, label] of [['start', '启动'], ['stop', '停止']]) {
      const button = document.createElement('button');
      button.className = action === 'start' ? 'button primary' : 'button';
      button.dataset.action = action;
      button.textContent = label;
      button.setAttribute('aria-label', `${label} ${app.name}`);
      button.onclick = () => run(action, app.id);
      actions.append(button);
    }
    row.append(name, state, environment, actions);
    $('application-sessions').append(row);
  }
}

async function initialize() {
  try {
    const result = await request('load');
    buildForm(result.timezones);
    applyValues(result.values);
    buildSessions(result.status.applications);
    updateStatus(result.status);
    showPage(result.page || 'general');
    showView(result.view || 'overview');
    $('loading').hidden = true;
  } catch (error) { $('loading').textContent = `加载失败，${error.message}`; }
}
initialize();

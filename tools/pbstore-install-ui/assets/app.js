'use strict';

// The page of the PB Installer. It holds no install logic: every action is a call to the program on this
// computer (api/...). Text that comes from the catalog or from a package is only ever set as text.

const STATE_TEXT = {
  'not-installed': 'not installed',
  'installed': 'installed',
  'outside-installer': 'installed outside the installer',
  'unknown': 'on the reader, not in the catalog',
};

const CONFIRMATION_TEXT = {
  'plan': 'I have read the plan and want it carried out.',
  'edited-files': 'Files of this app were changed since the install. Replace or remove them anyway.',
  'unknown-firmware': 'The firmware version of the reader cannot be read. Go on without checking it.',
  'unknown-model': 'The model of the reader cannot be read. Go on without checking that it is an InkPad One.',
  'existing-data-folder': 'A data folder of this name already exists and no installed app owns it. Use it as it is; its contents stay untouched.',
  'replace': 'This app was installed outside the installer. Replace its files as listed in the plan.',
  'repair': 'Carry out the repair of the interrupted operation.',
};

const ACTION_TEXT = { install: 'Install', update: 'Update', remove: 'Remove', adopt: 'Adopt', replace: 'Replace' };

const view = {
  reader: null,
  plan: null,       // the answer of api/plan or api/preview
  pending: null,    // {action, app_id} of the plan on screen
  catalog: false,
  busyTimer: null,
};

function $(id) { return document.getElementById(id); }

function el(tag, attributes, ...children) {
  const e = document.createElement(tag);
  if (attributes) {
    for (const key of Object.keys(attributes)) {
      if (key === 'class') e.className = attributes[key];
      else if (key === 'text') e.textContent = attributes[key];
      else e.setAttribute(key, attributes[key]);
    }
  }
  for (const child of children) {
    if (child === null || child === undefined) continue;
    e.appendChild(typeof child === 'string' ? document.createTextNode(child) : child);
  }
  return e;
}

function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

async function call(path, body) {
  const options = body === undefined ? { method: 'GET' } : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
  let response;
  try {
    response = await fetch(path, options);
  } catch (error) {
    return { status: 0, ok: false, data: null };
  }
  let data = null;
  try { data = await response.json(); } catch (error) { data = null; }
  return { status: response.status, ok: response.ok, data: data };
}

function show(id) { $(id).hidden = false; }
function hide(id) { $(id).hidden = true; }

function showStep(name) {
  for (const id of ['step-reader', 'step-apps', 'step-plan', 'step-progress', 'step-result']) hide(id);
  show(name);
  window.scrollTo(0, 0);
}

// ---- messages: what happened, whether the reader changed, what to do next -----------------------------------

function showMessage(answer, good) {
  const banner = $('banner');
  clear(banner);
  banner.className = good ? 'banner ok' : 'banner';
  if (!answer || !answer.message) {
    banner.appendChild(el('h3', { text: good ? 'Done' : 'Something went wrong' }));
    banner.appendChild(el('p', { text: good ? '' : 'The installer on this computer did not answer. Reload the page, or start the installer again.' }));
    banner.hidden = false;
    return;
  }
  const m = answer.message;
  banner.appendChild(el('h3', { text: good ? 'Done' : 'This could not be done' }));
  banner.appendChild(el('p', {}, el('span', { class: 'label', text: 'What happened: ' }), m.what));
  banner.appendChild(el('p', {}, el('span', { class: 'label', text: 'The reader: ' }), m.reader_effect));
  if (m.next) banner.appendChild(el('p', {}, el('span', { class: 'label', text: 'What to do next: ' }), m.next));
  if (answer.missing && answer.missing.length) {
    banner.appendChild(el('p', { text: 'Missing confirmations: ' + answer.missing.join(', ') }));
  }
  if (answer.details && answer.details.length > 1) {
    const list = el('ul');
    for (const line of answer.details) list.appendChild(el('li', { text: line }));
    banner.appendChild(el('details', {}, el('summary', { text: 'Technical details' }), list));
  }
  banner.hidden = false;
  banner.focus();
}

function hideMessage() { clear($('banner')); hide('banner'); }

function plainBanner(title, text) {
  const banner = $('banner');
  clear(banner);
  banner.className = 'banner ok';
  banner.appendChild(el('h3', { text: title }));
  if (text) banner.appendChild(el('p', { text: text }));
  banner.hidden = false;
}

// ---- step 1: the reader --------------------------------------------------------------------------------------

async function loadReaders() {
  const box = $('readers');
  clear(box);
  box.appendChild(el('p', { text: 'Looking for the reader ...' }));
  const r = await call('api/readers');
  clear(box);
  if (!r.ok || !r.data) {
    showMessage(r.data, false);
    return;
  }
  const data = r.data;
  if (data.candidates.length === 0) box.appendChild(el('p', { text: 'No drive that could be the reader was found.' }));
  for (const c of data.candidates) {
    const row = el('div', { class: 'row' });
    row.appendChild(el('strong', { text: c.label }));
    row.appendChild(document.createTextNode(c.mount_point + ', ' + c.size_text + ' '));
    if (c.qualifies) {
      const button = el('button', { type: 'button', class: 'primary', text: 'Use this reader' });
      button.addEventListener('click', () => openReader(c.mount_point));
      row.appendChild(button);
    } else {
      row.appendChild(document.createTextNode('(not a reader: missing ' + c.missing.join(' and ') + ')'));
    }
    box.appendChild(row);
  }
  for (const note of data.notes) box.appendChild(el('p', { text: note }));
  if (data.message && data.candidates.filter((c) => c.qualifies).length !== 1) {
    box.appendChild(el('p', {}, el('span', { class: 'label', text: 'Note: ' }), data.message.what));
    box.appendChild(el('p', { class: 'small', text: data.message.next }));
  }
}

async function openReader(folder) {
  hideMessage();
  const r = await call('api/reader/open', folder ? { folder: folder } : {});
  if (!r.ok) {
    showMessage(r.data, false);
    return;
  }
  view.reader = r.data.reader;
  $('reader-line').textContent = 'Reader: ' + view.reader.label + ' (' + view.reader.mount_point + ', ' + view.reader.size_text + ')';
  view.catalog = false;
  clear($('apps'));
  $('catalog-line').textContent = '';
  showStep('step-apps');
  await loadInstalled();
}

async function browse(path) {
  const r = await call('api/folders?path=' + encodeURIComponent(path || ''));
  const list = $('browse-list');
  clear(list);
  if (!r.ok) {
    $('browse-path').textContent = r.data && r.data.message ? r.data.message.what : 'This folder cannot be shown.';
    return;
  }
  $('browse-path').textContent = r.data.path ? 'In: ' + r.data.path : 'Places to start from:';
  if (r.data.path) {
    const up = el('button', { type: 'button', text: 'Up' });
    up.addEventListener('click', () => browse(r.data.parent));
    list.appendChild(el('li', {}, up));
  }
  for (const f of r.data.folders) {
    const enter = el('button', { type: 'button', text: f.name });
    enter.addEventListener('click', () => { $('folder-input').value = f.path; browse(f.path); });
    list.appendChild(el('li', {}, enter));
  }
}

// ---- step 2: the apps ----------------------------------------------------------------------------------------

function stateLabel(app) {
  let text = STATE_TEXT[app.state] || app.state_text;
  if (app.state === 'installed' && app.update_available) text = 'update available';
  const cls = app.state === 'installed' ? (app.update_available ? 'state state-update' : 'state state-installed')
    : app.state === 'outside-installer' ? 'state state-outside'
    : app.state === 'unknown' ? 'state state-unknown' : 'state state-not-installed';
  return el('span', { class: cls, text: text });
}

function renderApps(listing) {
  const box = $('apps');
  clear(box);
  if (!listing.apps.length) {
    box.appendChild(el('p', { text: listing.catalog_loaded ? 'The catalog lists no apps and the reader has none.' : 'No apps were found on the reader.' }));
    return;
  }
  const table = el('table');
  table.appendChild(el('thead', {}, el('tr', {}, el('th', { text: 'App' }), el('th', { text: 'State' }), el('th', { text: 'Installed' }), el('th', { text: 'Offered' }), el('th', { text: 'More' }), el('th', { text: 'Actions' }))));
  const body = el('tbody');
  for (const app of listing.apps) {
    const more = [];
    if (app.description) more.push(app.description);
    if (app.source) more.push('source: ' + app.source);
    if (app.from_local) more.push('not from the catalog, not signed by the maintainer');
    if (app.changed) more.push('files changed since the install');
    if (app.withdrawn) more.push('withdrawn in the catalog');
    if (app.menu_entry_missing) more.push('menu entry missing');
    if (app.state === 'outside-installer') more.push('The installer found a menu entry and files it did not install, so it cannot remove this app. It can adopt it if the files equal the catalog package (it compares them when you choose Adopt), or replace it after you confirm the list of changes. Your data is not touched.');
    const actions = el('td');
    for (const name of app.actions) {
      const button = el('button', { type: 'button', text: ACTION_TEXT[name] || name });
      button.setAttribute('aria-label', (ACTION_TEXT[name] || name) + ' ' + app.name);
      button.addEventListener('click', () => makePlan(name, app));
      actions.appendChild(button);
    }
    body.appendChild(el('tr', {}, el('td', {}, el('strong', { text: app.name }), el('br'), el('span', { class: 'small', text: app.id })), el('td', {}, stateLabel(app)),
      el('td', { text: app.installed_version || '-' }), el('td', { text: app.offered_version || '-' }), el('td', { class: 'small', text: more.join('; ') }), actions));
  }
  table.appendChild(body);
  box.appendChild(table);
}

async function showInterrupted(listing) {
  const box = $('interrupted');
  clear(box);
  if (!listing.interrupted) { box.hidden = true; return; }
  // what the repair will do, as it is now
  const plan = await call('api/repair/plan', {});
  const text = plan.ok && plan.data.interrupted ? plan.data.text : listing.interrupted.text;
  box.hidden = false;
  box.className = 'banner';
  box.appendChild(el('h3', { text: 'An earlier operation was interrupted' }));
  box.appendChild(el('p', { class: 'plan', text: text }));
  box.appendChild(el('p', { text: 'The reader needs repair (repair needed) before anything else can be done.' }));
  const check = el('input', { type: 'checkbox', id: 'repair-ok' });
  const label = el('label', { for: 'repair-ok', text: CONFIRMATION_TEXT.repair });
  const button = el('button', { type: 'button', class: 'primary', text: 'Repair now' });
  button.disabled = true;
  check.addEventListener('change', () => { button.disabled = !check.checked; });
  button.addEventListener('click', () => runRepair());
  box.appendChild(el('div', { class: 'confirm' }, check, label));
  box.appendChild(el('div', { class: 'row' }, button));
}

async function loadInstalled() {
  const r = await call('api/installed');
  if (!r.ok) { showMessage(r.data, false); return; }
  await showInterrupted(r.data);
  renderApps(r.data);
}

async function loadCatalog() {
  hideMessage();
  showWorking('Downloading the catalog');
  const r = await call('api/list', {});
  stopWorking();
  showStep('step-apps');
  if (!r.ok) { showMessage(r.data, false); return; }
  view.catalog = true;
  $('catalog-line').textContent = 'Catalog generated ' + r.data.catalog_generated_at + '.';
  await showInterrupted(r.data);
  renderApps(r.data);
  for (const note of (r.data.notices || [])) if (note.indexOf('W-INS-03') >= 0) plainBanner('First use', 'No earlier catalog was on record on this computer; this catalog is accepted because its signature is genuine.');
}

async function runRepair() {
  hideMessage();
  showWorking('Repairing the interrupted operation');
  const r = await call('api/repair', { confirmations: ['repair'] });
  stopWorking();
  showStep('step-apps');
  if (!r.ok) { showMessage(r.data, false); return; }
  plainBanner('Repaired', r.data.text);
  await loadInstalled();
}

// ---- step 3: the plan ----------------------------------------------------------------------------------------

async function makePlan(action, app, preview) {
  hideMessage();
  let body = { action: action, app_id: app.id };
  if (action === 'remove' && !preview) {
    body.delete_data = $('opt-delete-data').checked;
  }
  view.pending = { action: action, app: app, body: body };
  await requestPlan(body, preview);
}

async function requestPlan(body, preview) {
  showWorking(preview ? 'Preparing the preview' : 'Preparing the plan');
  const r = await call(preview ? 'api/preview' : 'api/plan', body);
  stopWorking();
  if (!r.ok) { showStep('step-apps'); showMessage(r.data, false); return; }
  if (r.data.nothing_to_do) { showStep('step-apps'); plainBanner('Nothing to do', r.data.text); return; }
  view.plan = r.data;
  renderPlan(r.data);
}

// The plan text as shown on the page: the lines of unchanged files ("  keep ...") become one line, because an app can
// have hundreds of them; the table under "All files" lists every file. The text of the plan itself is not changed.
function condensePlanText(text) {
  const out = [];
  let kept = 0;
  const flush = () => {
    if (kept > 0) out.push('  ... ' + kept + (kept === 1 ? ' unchanged file' : ' unchanged files') + ' (listed under "All files")');
    kept = 0;
  };
  for (const line of text.split('\n')) {
    if (/^ {2}keep {4}/.test(line)) { kept++; continue; }
    flush();
    out.push(line);
  }
  flush();
  return out.join('\n');
}

function renderPlan(plan) {
  showStep('step-plan');
  const s = plan.structured;
  const sum = s.summary;
  $('plan-summary').textContent = sum.files_to_change + ' files to change, ' + sum.bytes_text + ' to write; space needed ' + sum.space_needed_text + ', free ' + sum.space_free_text + '.';
  $('plan-text').textContent = condensePlanText(plan.text);
  const list = $('plan-file-list');
  clear(list);
  const table = el('table');
  table.appendChild(el('thead', {}, el('tr', {}, el('th', { text: 'What' }), el('th', { text: 'File' }), el('th', { text: 'Size' }), el('th', { text: 'SHA-256' }), el('th', { text: 'Note' }))));
  const body = el('tbody');
  for (const item of s.items) {
    body.appendChild(el('tr', {}, el('td', { text: item.op }), el('td', { text: item.path }), el('td', { text: item.size_text }), el('td', { class: 'small', text: item.sha256.substring(0, 16) }), el('td', { text: item.reason })));
  }
  table.appendChild(body);
  list.appendChild(table);
  $('plan-files').hidden = s.items.length === 0;

  const box = $('confirmation-list');
  clear(box);
  $('preview-note').hidden = !plan.dry_run_only;
  const boxes = [];
  for (const name of plan.confirmations) {
    const id = 'confirm-' + name;
    const check = el('input', { type: 'checkbox', id: id });
    check.dataset.name = name;
    box.appendChild(el('div', { class: 'confirm' }, check, el('label', { for: id, text: CONFIRMATION_TEXT[name] || name })));
    boxes.push(check);
  }
  const update = () => { $('carry-out').disabled = plan.dry_run_only || !boxes.every((b) => b.checked); };
  for (const b of boxes) b.addEventListener('change', update);
  update();
  $('carry-out').hidden = !!plan.dry_run_only;
  $('preview').hidden = !!plan.dry_run_only;
  $('carry-out').focus();
}

function checkedConfirmations() {
  const names = [];
  for (const check of document.querySelectorAll('#confirmation-list input[type="checkbox"]')) if (check.checked) names.push(check.dataset.name);
  return names;
}

async function carryOut() {
  hideMessage();
  const names = checkedConfirmations();
  showWorking('Writing to the reader');
  const r = await call('api/execute', { plan_id: view.plan.plan_id, confirmations: names });
  stopWorking();
  if (!r.ok) {
    showStep('step-apps');
    showMessage(r.data, false);
    await loadInstalled();
    return;
  }
  showResult(r.data);
}

function showResult(data) {
  showStep('step-result');
  $('result-text').textContent = data.text;
  const deletion = $('deletion');
  if (data.data_deletion) {
    deletion.hidden = false;
    view.deletion = data.data_deletion;
    $('deletion-text').textContent = data.data_deletion.text;
    $('question-1').textContent = data.data_deletion.question_1;
    $('question-2').textContent = data.data_deletion.question_2;
    $('token-1').value = '';
    $('token-2').value = '';
    $('delete-data').disabled = true;
  } else {
    deletion.hidden = true;
    view.deletion = null;
  }
}

async function deleteData() {
  hideMessage();
  const d = view.deletion;
  showWorking('Deleting the data folder');
  const r = await call('api/execute', { plan_id: d.plan_id, confirmations: ['delete-data-1', 'delete-data-2'], tokens: { folder: $('token-1').value.trim(), count: $('token-2').value.trim() } });
  stopWorking();
  showStep('step-result');
  if (!r.ok) { showMessage(r.data, false); return; }
  $('deletion').hidden = true;
  $('result-text').textContent = r.data.text;
}

// ---- working: progress and cancel ------------------------------------------------------------------------------

function showWorking(text) {
  showStep('step-progress');
  $('progress-text').textContent = text + ' ...';
  $('progress-bar').value = 0;
  $('progress-bar').removeAttribute('value');
  if (view.busyTimer) clearInterval(view.busyTimer);
  view.busyTimer = setInterval(pollState, 1000);
}

function stopWorking() {
  if (view.busyTimer) clearInterval(view.busyTimer);
  view.busyTimer = null;
}

async function pollState() {
  const r = await call('api/state');
  if (!r.ok || !r.data || !r.data.operation) return;
  const op = r.data.operation;
  $('progress-text').textContent = op.text + (op.total ? ' (' + op.done + ' of ' + op.total + ' ' + op.unit + ')' : '');
  if (op.total) { $('progress-bar').max = op.total; $('progress-bar').value = op.done; } else { $('progress-bar').removeAttribute('value'); }
}

// ---- wiring ----------------------------------------------------------------------------------------------------

function wire() {
  $('refresh-readers').addEventListener('click', loadReaders);
  $('use-folder').addEventListener('click', () => { const f = $('folder-input').value.trim(); if (f) openReader(f); });
  $('by-hand').addEventListener('toggle', () => { if ($('by-hand').open) browse(''); });
  $('load-catalog').addEventListener('click', loadCatalog);
  $('change-reader').addEventListener('click', () => { hideMessage(); showStep('step-reader'); loadReaders(); });
  $('plan-local').addEventListener('click', () => {
    const file = $('local-input').value.trim();
    if (!file) return;
    view.pending = { action: 'install-local', app: { id: '', name: 'the package' }, body: { action: 'install-local', local_file: file } };
    requestPlan(view.pending.body, false);
  });
  $('carry-out').addEventListener('click', carryOut);
  $('preview').addEventListener('click', () => requestPlan(view.pending.body, true));
  $('plan-back').addEventListener('click', () => { hideMessage(); showStep('step-apps'); });
  $('cancel').addEventListener('click', async () => { $('cancel').disabled = true; await call('api/cancel', {}); $('cancel').disabled = false; });
  $('back-to-apps').addEventListener('click', async () => { hideMessage(); showStep('step-apps'); await loadInstalled(); });
  $('close').addEventListener('click', async () => { await call('api/quit', {}); clear($('main')); $('main').appendChild(el('p', { text: 'The installer has been closed. You can close this page.' })); });
  const ready = () => { $('delete-data').disabled = !($('token-1').value.trim() && $('token-2').value.trim()); };
  $('token-1').addEventListener('input', ready);
  $('token-2').addEventListener('input', ready);
  $('delete-data').addEventListener('click', deleteData);
  setInterval(() => { call('api/ping', {}); }, 5000);
  call('api/ping', {});
}

wire();
showStep('step-reader');
loadReaders();

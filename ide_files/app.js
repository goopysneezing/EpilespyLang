// State Variables
let editor = null;
let activeTabPath = null;
let openTabs = []; // { path, name, isDirty, model, originalContent }
let isExecuting = false;
let executionEventSource = null;
let activeSidebarTab = 'explorer';

// Lucide Icon Config
function updateIcons() {
    if (window.lucide) {
        window.lucide.createIcons();
    }
}

// Show Toast Notifications
function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    
    let icon = 'info';
    if (type === 'success') icon = 'check-circle';
    if (type === 'error') icon = 'alert-triangle';
    
    toast.innerHTML = `
        <i data-lucide="${icon}"></i>
        <span class="toast-message">${message}</span>
    `;
    container.appendChild(toast);
    updateIcons();
    
    // Animate in
    setTimeout(() => toast.classList.add('show'), 10);
    
    // Remove after 3 seconds
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => toast.remove(), 300);
    }, 3000);
}

// Initialize Monaco Editor and Language Support
function initMonaco() {
    return new Promise((resolve) => {
        require(['vs/editor/editor.main'], function() {
            // 1. Register EpilespyLang
            monaco.languages.register({ id: 'epilespylang' });
            
            // 2. Define syntax highlighting rules (Monarch tokenizer)
            monaco.languages.setMonarchTokensProvider('epilespylang', {
                tokenizer: {
                    root: [
                        // Comments
                        [/#.*/, 'comment'],
                        [/\/\/.*/, 'comment'],
                        
                        // Strings
                        [/"([^"\\]|\\.)*"/, 'string'],
                        
                        // Keywords
                        [/\b(if|else|while|for|switch|exit|quit|help)\b/, 'keyword'],
                        
                        // Built-in functions
                        [/\b(print|input|len|num|str)\b/, 'keyword.function'],
                        
                        // Numbers
                        [/\b\d+(\.\d+)?\b/, 'number'],
                        
                        // Brackets and brackets
                        [/[{}()\[\]]/, 'brackets'],
                        
                        // Operators
                        [/[=+\-*/%&|!<>]+/, 'operator'],
                        
                        // Identifiers
                        [/[a-zA-Z_][a-zA-Z0-9_]*/, 'variable']
                    ]
                }
            });
            
            // 3. Language configuration
            monaco.languages.setLanguageConfiguration('epilespylang', {
                comments: {
                    lineComment: '#'
                },
                brackets: [
                    ['{', '}'],
                    ['[', ']'],
                    ['(', ')']
                ],
                autoClosingPairs: [
                    { open: '{', close: '}' },
                    { open: '[', close: ']' },
                    { open: '(', close: ')' },
                    { open: '"', close: '"' }
                ],
                surroundingPairs: [
                    { open: '{', close: '}' },
                    { open: '[', close: ']' },
                    { open: '(', close: ')' },
                    { open: '"', close: '"' }
                ]
            });

            // 4. Create Editor Instance
            const container = document.getElementById('monaco-editor-instance');

            editor = monaco.editor.create(container, {
                theme: 'vs-dark',
                fontSize: 14,
                fontFamily: "'JetBrains Mono', 'Fira Code', Consolas, monospace",
                fontLigatures: true,
                minimap: { enabled: true },
                automaticLayout: true,
                wordWrap: 'on',
                tabSize: 4,
                cursorBlinking: 'smooth',
                cursorSmoothCaretAnimation: 'on',
                padding: { top: 12, bottom: 12 },
                scrollbar: {
                    vertical: 'visible',
                    horizontal: 'visible',
                    useShadows: false,
                    verticalScrollbarSize: 10,
                    horizontalScrollbarSize: 10
                }
            });

            // Handle selection and cursor changes
            editor.onDidChangeCursorPosition((e) => {
                document.getElementById('status-cursor').textContent = `Line ${e.position.lineNumber}, Col ${e.position.column}`;
            });

            // Listen to content changes to mark tabs as dirty
            editor.onDidChangeModelContent(() => {
                const tab = openTabs.find(t => t.path === activeTabPath);
                if (tab) {
                    const currentVal = editor.getValue();
                    const isDirty = currentVal !== tab.originalContent;
                    if (tab.isDirty !== isDirty) {
                        tab.isDirty = isDirty;
                        renderTabs();
                    }
                }
            });

            // Custom commands / Shortcuts
            editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => {
                saveCurrentFile();
            });

            resolve();
        });
    });
}

// Helper to determine Monaco Editor language ID from file extension
function getLanguageId(filePath) {
    const ext = filePath.split('.').pop().toLowerCase();
    if (ext === 'ep') return 'epilespylang';
    if (ext === 'cpp' || ext === 'hpp') return 'cpp';
    if (ext === 'md') return 'markdown';
    if (ext === 'json') return 'json';
    return 'plaintext';
}

// Helper to get file icons
function getFileIconClass(filePath) {
    const ext = filePath.split('.').pop().toLowerCase();
    if (ext === 'ep') return 'file-icon-ep';
    if (ext === 'cpp') return 'file-icon-cpp';
    if (ext === 'hpp') return 'file-icon-hpp';
    if (ext === 'md') return 'file-icon-md';
    return 'file-icon-generic';
}

function getFileIcon(filePath) {
    const ext = filePath.split('.').pop().toLowerCase();
    if (ext === 'ep') return 'code';
    if (ext === 'cpp' || ext === 'hpp') return 'terminal';
    if (ext === 'md') return 'book-open';
    return 'file';
}

// ----------------------------------------------------
// File Explorer and API Operations
// ----------------------------------------------------

async function loadWorkspace() {
    const listContainer = document.getElementById('file-explorer-list');
    try {
        const response = await fetch('/api/files');
        if (!response.ok) throw new Error('Failed to retrieve workspace files.');
        const files = await response.json();
        
        if (files.length === 0) {
            listContainer.innerHTML = `<div class="empty-state">No files in workspace.</div>`;
            return;
        }

        listContainer.innerHTML = '';
        files.forEach(file => {
            const item = document.createElement('div');
            item.className = `explorer-item ${file.path === activeTabPath ? 'active-file' : ''}`;
            item.dataset.path = file.path;
            
            const isDir = file.isDir;
            const icon = isDir ? 'folder' : getFileIcon(file.path);
            const iconClass = isDir ? 'file-icon-folder' : getFileIconClass(file.path);

            item.innerHTML = `
                <span class="explorer-item-icon ${iconClass}">
                    <i data-lucide="${icon}"></i>
                </span>
                <span class="explorer-item-name" title="${file.name}">${file.name}</span>
                <span class="explorer-item-actions">
                    <button class="btn-item-rename" title="Rename"><i data-lucide="edit-3"></i></button>
                    <button class="btn-item-delete" title="Delete"><i data-lucide="trash-2"></i></button>
                </span>
            `;

            // Click file to open
            item.addEventListener('click', (e) => {
                if (e.target.closest('.explorer-item-actions')) return; // ignore action button clicks
                if (!isDir) {
                    openFile(file.path);
                }
            });

            // Delete action button
            item.querySelector('.btn-item-delete').addEventListener('click', (e) => {
                e.stopPropagation();
                confirmDeleteFile(file.path, file.name);
            });

            // Rename action button
            item.querySelector('.btn-item-rename').addEventListener('click', (e) => {
                e.stopPropagation();
                promptRenameFile(file.path, file.name);
            });

            listContainer.appendChild(item);
        });

        updateIcons();
    } catch (err) {
        showToast(err.message, 'error');
        listContainer.innerHTML = `<div class="empty-state text-danger">Error loading workspace</div>`;
    }
}

// Open File Content in Editor
async function openFile(filePath) {
    // 1. Check if tab is already open
    let tab = openTabs.find(t => t.path === filePath);
    
    if (!tab) {
        try {
            const response = await fetch(`/api/read?path=${encodeURIComponent(filePath)}`);
            if (!response.ok) throw new Error(`Could not read file: ${filePath}`);
            const data = await response.json();
            
            // Create a new Monaco text model
            const lang = getLanguageId(filePath);
            const model = monaco.editor.createModel(data.content, lang);
            
            tab = {
                path: filePath,
                name: filePath.split(/[/\\]/).pop(),
                isDirty: false,
                model: model,
                originalContent: data.content
            };
            openTabs.push(tab);
        } catch (err) {
            showToast(err.message, 'error');
            return;
        }
    }

    // 2. Set Active Tab
    activeTabPath = filePath;
    
    // Switch Monaco instance model
    document.getElementById('empty-editor-message').style.display = 'none';
    document.getElementById('monaco-editor-instance').style.display = 'block';
    editor.setModel(tab.model);
    editor.focus();

    // 3. Update UI states
    renderTabs();
    updateStatusBar();
    highlightExplorerActiveFile();
}

// Highlight the active file in Explorer
function highlightExplorerActiveFile() {
    document.querySelectorAll('.explorer-item').forEach(item => {
        if (item.dataset.path === activeTabPath) {
            item.classList.add('active-file');
        } else {
            item.classList.remove('active-file');
        }
    });
}

// Render Tabs Bar
function renderTabs() {
    const container = document.getElementById('tabs-container');
    container.innerHTML = '';

    openTabs.forEach(tab => {
        const item = document.createElement('div');
        item.className = `tab-item ${tab.path === activeTabPath ? 'active' : ''} ${tab.isDirty ? 'dirty' : ''}`;
        
        const iconClass = getFileIconClass(tab.path);
        const icon = getFileIcon(tab.path);

        item.innerHTML = `
            <span class="tab-item-icon ${iconClass}">
                <i data-lucide="${icon}"></i>
            </span>
            <span class="tab-item-name">${tab.name}</span>
            <button class="tab-close-btn" title="Close File"><i data-lucide="x"></i></button>
        `;

        item.addEventListener('click', (e) => {
            if (e.target.closest('.tab-close-btn')) return;
            openFile(tab.path);
        });

        item.querySelector('.tab-close-btn').addEventListener('click', (e) => {
            e.stopPropagation();
            closeFile(tab.path);
        });

        container.appendChild(item);
    });

    updateIcons();
}

// Close File
function closeFile(filePath) {
    const tabIdx = openTabs.findIndex(t => t.path === filePath);
    if (tabIdx === -1) return;

    const tab = openTabs[tabIdx];
    if (tab.isDirty) {
        if (!confirm(`"${tab.name}" has unsaved changes. Discard changes and close?`)) {
            return;
        }
    }

    // Dispose model to free memory
    tab.model.dispose();
    openTabs.splice(tabIdx, 1);

    if (activeTabPath === filePath) {
        if (openTabs.length > 0) {
            // Open previous or next tab
            const nextActiveIdx = Math.max(0, tabIdx - 1);
            openFile(openTabs[nextActiveIdx].path);
        } else {
            // No tabs left
            activeTabPath = null;
            document.getElementById('monaco-editor-instance').style.display = 'none';
            document.getElementById('empty-editor-message').style.display = 'flex';
            updateStatusBar();
            highlightExplorerActiveFile();
        }
    }

    renderTabs();
}

// Save Current Active File
async function saveCurrentFile() {
    if (!activeTabPath) return;
    const tab = openTabs.find(t => t.path === activeTabPath);
    if (!tab || !tab.isDirty) return;

    const currentVal = editor.getValue();
    try {
        const response = await fetch('/api/write', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                path: tab.path,
                content: currentVal
            })
        });

        if (!response.ok) throw new Error('Failed to save file.');

        tab.originalContent = currentVal;
        tab.isDirty = false;
        renderTabs();
        showToast(`Saved ${tab.name} successfully`, 'success');
    } catch (err) {
        showToast(err.message, 'error');
    }
}

// Update Bottom Status Bar info
function updateStatusBar() {
    const statusText = document.getElementById('status-filename');
    const statusLang = document.getElementById('status-lang');
    
    if (activeTabPath) {
        statusText.textContent = activeTabPath;
        const ext = activeTabPath.split('.').pop().toLowerCase();
        if (ext === 'ep') statusLang.textContent = 'EpilespyLang';
        else if (ext === 'cpp' || ext === 'hpp') statusLang.textContent = 'C++ Source';
        else if (ext === 'md') statusLang.textContent = 'Markdown';
        else statusLang.textContent = ext.toUpperCase();
    } else {
        statusText.textContent = 'No File Open';
        statusLang.textContent = 'EpilespyLang';
        document.getElementById('status-cursor').textContent = 'Line 0, Col 0';
    }
}

// ----------------------------------------------------
// Terminal Console Logging and SSE
// ----------------------------------------------------

function appendTerminalRow(text, type = 'stdout') {
    const terminalBody = document.getElementById('terminal-content');
    const row = document.createElement('div');
    row.className = `terminal-row ${type}`;
    row.textContent = text;
    terminalBody.appendChild(row);
    terminalBody.scrollTop = terminalBody.scrollHeight;
}

function clearTerminal() {
    document.getElementById('terminal-content').innerHTML = '';
}

// Set general IDE running modes
function setMode(modeState, text) {
    const modeIndicator = document.getElementById('status-mode');
    const modeText = document.getElementById('status-mode-text');
    
    modeIndicator.className = `status-indicator-pill ${modeState}`;
    modeText.textContent = text;
}

// Stream Script Execution Logs
function runActiveScript() {
    if (isExecuting) return;
    if (!activeTabPath) {
        showToast('Open a file first to run execution.', 'info');
        return;
    }

    const tab = openTabs.find(t => t.path === activeTabPath);
    // If dirty, save automatically first
    if (tab && tab.isDirty) {
        saveCurrentFile().then(() => startExecution());
    } else {
        startExecution();
    }
}

function startExecution() {
    isExecuting = true;
    clearTerminal();
    appendTerminalRow(`[System] Executing script: ${activeTabPath}\n`, 'system');
    
    // Toggle buttons
    document.querySelectorAll('.btn-run').forEach(btn => btn.disabled = true);
    document.getElementById('btn-stop').disabled = false;
    setMode('running', 'Running...');

    const runUrl = `/api/run-stream?path=${encodeURIComponent(activeTabPath)}`;
    executionEventSource = new EventSource(runUrl);

    executionEventSource.onmessage = function(event) {
        const payload = JSON.parse(event.data);
        if (payload.type === 'stdout') {
            appendTerminalRow(payload.text, 'stdout');
        } else if (payload.type === 'stderr') {
            appendTerminalRow(payload.text, 'stderr');
        } else if (payload.type === 'system') {
            appendTerminalRow(payload.text, 'system');
        } else if (payload.type === 'close') {
            appendTerminalRow(`\n[System] Process exited with code ${payload.code}\n`, payload.code === 0 ? 'success' : 'stderr');
            endExecution();
        }
    };

    executionEventSource.onerror = function(err) {
        appendTerminalRow(`[Error] Execution stream disconnected unexpectedly.\n`, 'stderr');
        endExecution();
    };
}

function stopExecution() {
    if (!isExecuting) return;
    
    if (executionEventSource) {
        executionEventSource.close();
        executionEventSource = null;
    }
    
    // Trigger termination signal on backend
    fetch('/api/stop', { method: 'POST' }).then(() => {
        appendTerminalRow(`\n[System] Execution terminated by user.\n`, 'stderr');
        endExecution();
    });
}

function endExecution() {
    isExecuting = false;
    if (executionEventSource) {
        executionEventSource.close();
        executionEventSource = null;
    }
    document.querySelectorAll('.btn-run').forEach(btn => btn.disabled = false);
    document.getElementById('btn-stop').disabled = true;
    setMode('ready', 'Ready');
}

// Stream Compiler Builds
function buildWorkspaceInterpreter() {
    if (isExecuting) return;
    isExecuting = true;
    clearTerminal();
    appendTerminalRow(`[Compiler] Triggering build task for C++ interpreter (main.cpp)...\n`, 'system');
    
    document.querySelectorAll('.btn-run').forEach(btn => btn.disabled = true);
    setMode('busy', 'Compiling...');

    const buildUrl = `/api/build`;
    const buildEventSource = new EventSource(buildUrl);

    buildEventSource.onmessage = function(event) {
        const payload = JSON.parse(event.data);
        if (payload.type === 'stdout') {
            appendTerminalRow(payload.text, 'stdout');
        } else if (payload.type === 'stderr') {
            appendTerminalRow(payload.text, 'stderr');
        } else if (payload.type === 'close') {
            if (payload.code === 0) {
                appendTerminalRow(`\n[Compiler] EpilespyLang.exe compiled successfully!\n`, 'success');
                showToast('Interpreter compiled successfully', 'success');
            } else {
                appendTerminalRow(`\n[Compiler] Compile failed with exit code ${payload.code}.\n`, 'stderr');
                showToast('Compilation failed', 'error');
            }
            buildEventSource.close();
            endExecution();
        }
    };

    buildEventSource.onerror = function() {
        appendTerminalRow(`[Error] Compilation stream disconnected.\n`, 'stderr');
        buildEventSource.close();
        endExecution();
    };
}

// ----------------------------------------------------
// Modals Prompt Triggers (CRUD Dialogs)
// ----------------------------------------------------

let modalSubmitCallback = null;

function showPromptModal(title, label, placeholder, defaultVal, onSubmit) {
    const modal = document.getElementById('modal-file-prompt');
    const titleEl = document.getElementById('modal-prompt-title');
    const labelEl = document.getElementById('modal-prompt-label');
    const inputEl = document.getElementById('modal-input-name');
    const errorEl = document.getElementById('modal-error-text');

    titleEl.textContent = title;
    labelEl.textContent = label;
    inputEl.placeholder = placeholder;
    inputEl.value = defaultVal || '';
    errorEl.textContent = '';

    modal.classList.add('active');
    inputEl.focus();
    inputEl.select();

    modalSubmitCallback = async () => {
        const value = inputEl.value.trim();
        if (!value) {
            errorEl.textContent = 'Name cannot be empty';
            return;
        }
        
        try {
            await onSubmit(value);
            modal.classList.remove('active');
        } catch (err) {
            errorEl.textContent = err.message;
        }
    };
}

function closePromptModal() {
    document.getElementById('modal-file-prompt').classList.remove('active');
    modalSubmitCallback = null;
}

// New File dialog trigger
function createNewFilePrompt(isFolder = false) {
    const title = isFolder ? 'Create New Folder' : 'Create New File';
    const label = isFolder ? 'Folder name:' : 'File name:';
    const placeholder = isFolder ? 'my_folder' : 'script.ep';
    
    showPromptModal(title, label, placeholder, '', async (name) => {
        const response = await fetch('/api/create', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, isFolder })
        });
        if (!response.ok) {
            const errData = await response.json();
            throw new Error(errData.error || 'Failed to create item.');
        }
        showToast(`Created ${name} successfully`, 'success');
        loadWorkspace();
    });
}

// Rename dialog trigger
function promptRenameFile(oldPath, oldName) {
    showPromptModal('Rename File', 'Enter new name:', 'script.ep', oldName, async (newName) => {
        if (newName === oldName) return;
        const response = await fetch('/api/rename', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ oldPath, newName })
        });
        if (!response.ok) {
            const errData = await response.json();
            throw new Error(errData.error || 'Failed to rename item.');
        }

        // Update open tabs if rename corresponds to opened path
        const tab = openTabs.find(t => t.path === oldPath);
        if (tab) {
            const newPath = oldPath.substring(0, oldPath.lastIndexOf(oldName)) + newName;
            tab.path = newPath;
            tab.name = newName;
            
            // If active tab, update activeTabPath
            if (activeTabPath === oldPath) {
                activeTabPath = newPath;
            }
        }

        showToast(`Renamed ${oldName} to ${newName}`, 'success');
        loadWorkspace();
        renderTabs();
        updateStatusBar();
    });
}

// Delete confirmation dialog trigger
function confirmDeleteFile(filePath, fileName) {
    const modal = document.getElementById('modal-confirm');
    const textEl = document.getElementById('modal-confirm-text');
    
    textEl.textContent = `Are you sure you want to delete "${fileName}"? This action cannot be undone.`;
    modal.classList.add('active');

    const submitBtn = document.getElementById('modal-confirm-submit');
    const cancelBtn = document.getElementById('modal-confirm-cancel');

    const handleConfirm = async () => {
        try {
            const response = await fetch('/api/delete', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: filePath })
            });

            if (!response.ok) throw new Error('Could not delete file');

            // Force close tab if open
            const tab = openTabs.find(t => t.path === filePath);
            if (tab) {
                // Remove dirty flag to close silently
                tab.isDirty = false;
                closeFile(filePath);
            }

            showToast(`Deleted ${fileName}`, 'info');
            loadWorkspace();
        } catch (err) {
            showToast(err.message, 'error');
        } finally {
            modal.classList.remove('active');
            cleanupListeners();
        }
    };

    const handleCancel = () => {
        modal.classList.remove('active');
        cleanupListeners();
    };

    const cleanupListeners = () => {
        submitBtn.removeEventListener('click', handleConfirm);
        cancelBtn.removeEventListener('click', handleCancel);
    };

    submitBtn.addEventListener('click', handleConfirm);
    cancelBtn.addEventListener('click', handleCancel);
}

// ----------------------------------------------------
// Global Workspace Search
// ----------------------------------------------------

async function runGlobalSearch() {
    const query = document.getElementById('search-input').value.trim();
    const resultsContainer = document.getElementById('search-results-list');
    
    if (!query) {
        resultsContainer.innerHTML = '<div class="empty-state">Enter query to search files.</div>';
        return;
    }

    resultsContainer.innerHTML = `
        <div class="loading-spinner-container">
            <div class="spinner"></div>
            <span>Searching...</span>
        </div>
    `;

    try {
        const response = await fetch(`/api/search?query=${encodeURIComponent(query)}`);
        if (!response.ok) throw new Error('Search failed.');
        const results = await response.json();

        if (Object.keys(results).length === 0) {
            resultsContainer.innerHTML = '<div class="empty-state">No matches found.</div>';
            return;
        }

        resultsContainer.innerHTML = '';
        for (const [filePath, matches] of Object.entries(results)) {
            const fileItem = document.createElement('div');
            fileItem.className = 'search-result-file';
            
            const fileName = filePath.split(/[/\\]/).pop();
            const iconClass = getFileIconClass(filePath);
            const icon = getFileIcon(filePath);

            let matchesHtml = '';
            matches.forEach(m => {
                // Highlight matching words inside matching line content
                const escapedContent = m.lineContent
                    .replace(/&/g, "&amp;")
                    .replace(/</g, "&lt;")
                    .replace(/>/g, "&gt;");
                const regex = new RegExp(`(${query.replace(/[-\/\\^$*+?.()|[\]{}]/g, '\\$&')})`, 'gi');
                const highlighted = escapedContent.replace(regex, '<mark>$1</mark>');

                matchesHtml += `
                    <div class="search-result-match" data-line="${m.lineNum}">
                        <strong>Line ${m.lineNum}:</strong> ${highlighted}
                    </div>
                `;
            });

            fileItem.innerHTML = `
                <div class="search-result-file-header">
                    <span class="${iconClass}"><i data-lucide="${icon}"></i></span>
                    <span>${fileName}</span>
                </div>
                <div class="search-result-file-matches">
                    ${matchesHtml}
                </div>
            `;

            // Open file on clicking file header
            fileItem.querySelector('.search-result-file-header').addEventListener('click', () => {
                openFile(filePath);
            });

            // Open file and scroll to line on clicking individual match
            fileItem.querySelectorAll('.search-result-match').forEach(matchEl => {
                matchEl.addEventListener('click', () => {
                    const line = parseInt(matchEl.dataset.line);
                    openFile(filePath).then(() => {
                        if (editor) {
                            editor.revealLineInCenter(line);
                            editor.setPosition({ lineNumber: line, column: 1 });
                        }
                    });
                });
            });

            resultsContainer.appendChild(fileItem);
        }

        updateIcons();
    } catch (err) {
        showToast(err.message, 'error');
        resultsContainer.innerHTML = '<div class="empty-state text-danger">Error running search</div>';
    }
}

// ----------------------------------------------------
// Startup / Initialization and Event Listeners
// ----------------------------------------------------

window.addEventListener('DOMContentLoaded', async () => {
    // 1. Initial Icon Load
    updateIcons();

    // 2. Load Monaco Editor
    await initMonaco();

    // 3. Load Workspace File List
    loadWorkspace();

    // 4. Activity Bar Tab Clicking
    document.querySelectorAll('.activity-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const targetTab = btn.dataset.tab;
            
            // Toggle active state in activity bar buttons
            document.querySelectorAll('.activity-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');

            // Toggle active sidebar panels
            document.querySelectorAll('.sidebar-tab-content').forEach(p => p.classList.remove('active'));
            const activePanel = document.getElementById(`sidebar-${targetTab}`);
            if (activePanel) activePanel.classList.add('active');
            
            activeSidebarTab = targetTab;
            updateIcons();
        });
    });

    // 5. Connect Workspace CRUD Actions
    document.getElementById('btn-new-file').addEventListener('click', () => createNewFilePrompt(false));
    document.getElementById('btn-new-folder').addEventListener('click', () => createNewFilePrompt(true));
    document.getElementById('btn-refresh').addEventListener('click', loadWorkspace);

    // 6. Connect Execute controls
    document.getElementById('btn-run').addEventListener('click', runActiveScript);
    document.getElementById('btn-run-sidebar').addEventListener('click', runActiveScript);
    document.getElementById('btn-stop').addEventListener('click', stopExecution);
    document.getElementById('btn-build-sidebar').addEventListener('click', buildWorkspaceInterpreter);

    // 7. Modal cancellation bindings
    document.getElementById('modal-btn-cancel').addEventListener('click', closePromptModal);
    document.getElementById('modal-input-name').addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && modalSubmitCallback) {
            modalSubmitCallback();
        } else if (e.key === 'Escape') {
            closePromptModal();
        }
    });
    document.getElementById('modal-btn-submit').addEventListener('click', () => {
        if (modalSubmitCallback) modalSubmitCallback();
    });

    // 8. Terminal Action bindings
    document.getElementById('btn-clear-terminal').addEventListener('click', clearTerminal);
    
    // Collapse/Expand Terminal panel
    const toggleTerminalBtn = document.getElementById('btn-toggle-terminal');
    const terminalPane = document.getElementById('terminal-pane');
    let isTerminalCollapsed = false;

    toggleTerminalBtn.addEventListener('click', () => {
        if (isTerminalCollapsed) {
            terminalPane.style.height = '200px';
            toggleTerminalBtn.innerHTML = '<i data-lucide="chevron-down"></i>';
        } else {
            terminalPane.style.height = '36px';
            toggleTerminalBtn.innerHTML = '<i data-lucide="chevron-up"></i>';
        }
        isTerminalCollapsed = !isTerminalCollapsed;
        updateIcons();
        // Resize Monaco editor to fit new bounds
        if (editor) {
            setTimeout(() => editor.layout(), 250);
        }
    });

    // 9. Global Search input bindings
    document.getElementById('btn-run-search').addEventListener('click', runGlobalSearch);
    document.getElementById('search-input').addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            runGlobalSearch();
        }
    });

    // 10. Settings Configuration binds
    document.getElementById('setting-fontsize').addEventListener('change', (e) => {
        const val = parseInt(e.target.value);
        if (editor) {
            editor.updateOptions({ fontSize: val });
        }
    });

    document.getElementById('setting-minimap').addEventListener('change', (e) => {
        const isChecked = e.target.checked;
        if (editor) {
            editor.updateOptions({ minimap: { enabled: isChecked } });
        }
    });

    document.getElementById('setting-wordwrap').addEventListener('change', (e) => {
        const isChecked = e.target.checked;
        if (editor) {
            editor.updateOptions({ wordWrap: isChecked ? 'on' : 'off' });
        }
    });

    // 11. Custom Window keyboard shortcuts (F5 runs script, Ctrl + S saves file)
    window.addEventListener('keydown', (e) => {
        if (e.key === 'F5') {
            e.preventDefault();
            runActiveScript();
        } else if (e.ctrlKey && e.key === 's') {
            e.preventDefault();
            saveCurrentFile();
        }
    });
});

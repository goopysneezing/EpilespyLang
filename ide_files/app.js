// State Variables
let editor = null;
let activeTabPath = null;
let openTabs = []; // { path, name, isDirty, model, originalContent }
let isExecuting = false;
let executionEventSource = null;
let activeSidebarTab = 'explorer';

// 3D Scene Designer State
let designerMode = 'scene'; // 'scene' or 'code'
let threeScene = null;
let threeCamera = null;
let threeRenderer = null;
let threeOrbitControls = null;
let threeTransformControls = null;
let threeObjects = []; // { dataId, mesh, group }
let selectedThreeObject = null;
let boxHelper = null;
let parsedSceneData = [];
let raycaster = null;
let mousePosition = null;
let pointerDownX = 0;
let pointerDownY = 0;
let currentGizmoMode = 'translate'; // 'select', 'translate', 'scale', 'rotate'

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
                        [/\b(print|input|len|num|str|push|pop|type|window|gameWindow3D|gameWindow2D|sleep|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|sqrt|cbrt|pow|exp|log|log10|log2|abs|ceil|floor|round|min|max|deg2rad|rad2deg|epilepsy|fetch|sound|beep|readFile|writeFile|appendFile|exists|setGravity|enableGravity|setCameraMode|setSecondPersonCamera|jump|getPlayerX|getPlayerY|getPlayerZ|isGrounded)\b/, 'keyword.function'],
                        
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
    if (ext === 'epscene') return 'plaintext';
    return 'plaintext';
}

// Helper to get file icons
function getFileIconClass(filePath) {
    const ext = filePath.split('.').pop().toLowerCase();
    if (ext === 'ep') return 'file-icon-ep';
    if (ext === 'cpp') return 'file-icon-cpp';
    if (ext === 'hpp') return 'file-icon-hpp';
    if (ext === 'md') return 'file-icon-md';
    if (ext === 'epscene') return 'file-icon-epscene';
    return 'file-icon-generic';
}

function getFileIcon(filePath) {
    const ext = filePath.split('.').pop().toLowerCase();
    if (ext === 'ep') return 'code';
    if (ext === 'cpp' || ext === 'hpp') return 'terminal';
    if (ext === 'md') return 'book-open';
    if (ext === 'epscene') return 'map';
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

/* ==========================================================
   R O B L O X   S T U D I O   S C E N E   D E S I G N E R
   ========================================================== */

const epsceneColorMap = {
    'black': 0x000000,
    'white': 0xffffff,
    'red': 0xff0000,
    'green': 0x00ff00,
    'blue': 0x0000ff,
    'yellow': 0xffff00,
    'purple': 0x800080,
    'orange': 0xffa500,
    'cyan': 0x00ffff,
    'magenta': 0xff00ff,
    'gray': 0x808080,
    'lightgray': 0xd3d3d3,
    'darkgray': 0xa9a9a9,
    'brown': 0x8b4513,
    'saddlebrown': 0x8b4513,
    'forestgreen': 0x228b22
};

function getHexColor(colorName) {
    if (!colorName) return 0xffffff;
    if (colorName.startsWith('#')) {
        return parseInt(colorName.substring(1), 16);
    }
    return epsceneColorMap[colorName.toLowerCase()] || 0xffffff;
}

function getThreeMaterial(materialName, colorName) {
    const color = getHexColor(colorName);
    const mat = materialName ? materialName.toLowerCase() : 'plastic';
    
    switch (mat) {
        case 'neon':
            return new THREE.MeshBasicMaterial({
                color: color,
                toneMapped: false
            });
        case 'metal':
            return new THREE.MeshStandardMaterial({
                color: color,
                metalness: 0.8,
                roughness: 0.2
            });
        case 'glass':
            return new THREE.MeshPhysicalMaterial({
                color: color,
                transparent: true,
                opacity: 0.6,
                transmission: 0.9,
                roughness: 0.1,
                thickness: 1.0
            });
        case 'wood':
            return new THREE.MeshStandardMaterial({
                color: color,
                roughness: 0.8,
                metalness: 0.1
            });
        case 'plastic':
        default:
            return new THREE.MeshStandardMaterial({
                color: color,
                roughness: 0.5,
                metalness: 0.1
            });
    }
}

// Parse .epscene lines into structured JS objects
function parseEpscene(content) {
    const lines = content.split(/\r?\n/);
    const data = [];
    
    lines.forEach((line, index) => {
        const trimmed = line.trim();
        if (trimmed.startsWith('#') || trimmed === '') {
            data.push({
                type: trimmed.startsWith('#') ? 'comment' : 'blank',
                raw: line,
                id: 'meta_' + index + '_' + Math.random().toString(36).substr(2, 4)
            });
            return;
        }
        
        const tokens = trimmed.split(/\s+/);
        const command = tokens[0].toLowerCase();
        const id = 'obj_' + index + '_' + Math.random().toString(36).substr(2, 4);
        
        if (command === 'grid') {
            const x = parseFloat(tokens[1]) || 0;
            const z = parseFloat(tokens[2]) || 0;
            const size = parseFloat(tokens[3]) || 30;
            const spacing = parseFloat(tokens[4]) || 2;
            const color = tokens[5] || 'gray';
            
            data.push({ type: 'grid', x, z, size, spacing, color, id });
        } else if (command === 'cube' || command === 'box') {
            const x = parseFloat(tokens[1]) || 0;
            const y = parseFloat(tokens[2]) || 0;
            const z = parseFloat(tokens[3]) || 0;
            const sx = parseFloat(tokens[4]) || 1;
            const sy = parseFloat(tokens[5]) || 1;
            const sz = parseFloat(tokens[6]) || 1;
            const color = tokens[7] || 'white';
            const material = tokens[8] || 'Plastic';
            const anchored = tokens[9] === 'true';
            const canCollide = tokens[10] === 'true';
            const name = tokens.slice(11).join(' ') || 'Cube';
            
            data.push({ type: 'cube', x, y, z, sx, sy, sz, color, material, anchored, canCollide, name, id });
        } else if (command === 'tree') {
            const x = parseFloat(tokens[1]) || 0;
            const y = parseFloat(tokens[2]) || 0;
            const z = parseFloat(tokens[3]) || 0;
            const trunkHeight = parseFloat(tokens[4]) || 2;
            const foliageSize = parseFloat(tokens[5]) || 1.5;
            const material = tokens[6] || 'Plastic';
            const anchored = tokens[7] === 'true';
            const canCollide = tokens[8] === 'true';
            const name = tokens.slice(9).join(' ') || 'Tree';
            
            data.push({ type: 'tree', x, y, z, trunkHeight, foliageSize, material, anchored, canCollide, name, id });
        } else if (command === 'model') {
            const x = parseFloat(tokens[1]) || 0;
            const y = parseFloat(tokens[2]) || 0;
            const z = parseFloat(tokens[3]) || 0;
            const sx = parseFloat(tokens[4]) || 1;
            const sy = parseFloat(tokens[5]) || 1;
            const sz = parseFloat(tokens[6]) || 1;
            const rx = parseFloat(tokens[7]) || 0;
            const ry = parseFloat(tokens[8]) || 0;
            const rz = parseFloat(tokens[9]) || 0;
            const filePath = tokens[10] || 'assets/rock.obj';
            const color = tokens[11] || 'white';
            const material = tokens[12] || 'Plastic';
            const anchored = tokens[13] === 'true';
            const canCollide = tokens[14] === 'true';
            const name = tokens.slice(15).join(' ') || 'Model';
            
            data.push({ type: 'model', x, y, z, sx, sy, sz, rx, ry, rz, filePath, color, material, anchored, canCollide, name, id });
        } else {
            data.push({ type: 'unknown', raw: line, id });
        }
    });
    
    return data;
}

// Convert parsed JS objects back to .epscene text representation
function generateEpscene(data) {
    return data.map(item => {
        if (item.type === 'comment' || item.type === 'blank' || item.type === 'unknown') {
            return item.raw;
        }
        if (item.type === 'grid') {
            return `grid ${item.x} ${item.z} ${item.size} ${item.spacing} ${item.color}`;
        }
        if (item.type === 'cube') {
            return `cube ${item.x} ${item.y} ${item.z} ${item.sx} ${item.sy} ${item.sz} ${item.color} ${item.material} ${item.anchored} ${item.canCollide} ${item.name}`;
        }
        if (item.type === 'tree') {
            return `tree ${item.x} ${item.y} ${item.z} ${item.trunkHeight} ${item.foliageSize} ${item.material} ${item.anchored} ${item.canCollide} ${item.name}`;
        }
        if (item.type === 'model') {
            return `model ${item.x} ${item.y} ${item.z} ${item.sx} ${item.sy} ${item.sz} ${item.rx} ${item.ry} ${item.rz} ${item.filePath} ${item.color} ${item.material} ${item.anchored} ${item.canCollide} ${item.name}`;
        }
        return '';
    }).join('\n');
}

function initSceneDesigner() {
    if (typeof THREE === 'undefined') {
        showToast('Three.js CDN failed to load. Please check your network connection.', 'error');
        throw new Error('Three.js is not loaded.');
    }
    if (typeof THREE.OrbitControls === 'undefined') {
        showToast('Three.js OrbitControls CDN failed to load.', 'error');
        throw new Error('THREE.OrbitControls is not loaded.');
    }
    if (typeof THREE.TransformControls === 'undefined') {
        showToast('Three.js TransformControls CDN failed to load.', 'error');
        throw new Error('THREE.TransformControls is not loaded.');
    }

    if (sceneDesignerInitialized) {
        resizeThreeRenderer();
        setTimeout(resizeThreeRenderer, 50);
        setTimeout(resizeThreeRenderer, 150);
        return;
    }
    
    const container = document.getElementById('scene-canvas-container');
    if (!container) return;
    
    // Scene
    threeScene = new THREE.Scene();
    threeScene.background = new THREE.Color(0x0a0f1d);
    
    // Default grid helper
    const mainGrid = new THREE.GridHelper(100, 100, 0x1e293b, 0x0f172a);
    mainGrid.position.y = -0.01;
    threeScene.add(mainGrid);
    
    // Camera
    const initialAspect = (container.clientWidth && container.clientHeight) ? (container.clientWidth / container.clientHeight) : 1;
    threeCamera = new THREE.PerspectiveCamera(50, initialAspect, 0.1, 1000);
    threeCamera.position.set(0, 12, 18);
    
    // Renderer
    threeRenderer = new THREE.WebGLRenderer({ antialias: true });
    threeRenderer.setSize(container.clientWidth || 400, container.clientHeight || 300);
    threeRenderer.setPixelRatio(window.devicePixelRatio);
    threeRenderer.shadowMap.enabled = true;
    container.innerHTML = '';
    container.appendChild(threeRenderer.domElement);
    
    // Orbit Controls
    threeOrbitControls = new THREE.OrbitControls(threeCamera, threeRenderer.domElement);
    threeOrbitControls.enableDamping = true;
    threeOrbitControls.dampingFactor = 0.05;
    threeOrbitControls.maxPolarAngle = Math.PI / 2 - 0.01;
    
    // Lights
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
    threeScene.add(ambientLight);
    
    const dirLight = new THREE.DirectionalLight(0xffffff, 0.8);
    dirLight.position.set(20, 40, 20);
    dirLight.castShadow = true;
    threeScene.add(dirLight);
    
    const dirLight2 = new THREE.DirectionalLight(0x00f2fe, 0.3);
    dirLight2.position.set(-20, 20, -20);
    threeScene.add(dirLight2);
    
    // Transform Controls
    threeTransformControls = new THREE.TransformControls(threeCamera, threeRenderer.domElement);
    threeScene.add(threeTransformControls);
    
    threeTransformControls.addEventListener('change', () => {
        renderScene();
    });
    threeTransformControls.addEventListener('dragging-changed', (event) => {
        threeOrbitControls.enabled = !event.value;
        if (!event.value) {
            updateSelectedObjectPropertiesFromMesh();
        }
    });
    
    raycaster = new THREE.Raycaster();
    mousePosition = new THREE.Vector2();
    
    // Resize/Layout listener
    window.addEventListener('resize', resizeThreeRenderer);
    window.addEventListener('keydown', onDesignerKeyDown);
    
    initToolbarBindings();
    
    sceneDesignerInitialized = true;
    animateSceneDesigner();

    // Trigger delayed layout resizes to account for flex and display transitions
    setTimeout(resizeThreeRenderer, 50);
    setTimeout(resizeThreeRenderer, 150);
    setTimeout(resizeThreeRenderer, 350);
}

let sceneDesignerInitialized = false;

function resizeThreeRenderer() {
    const container = document.getElementById('scene-canvas-container');
    if (!container || !threeRenderer || !threeCamera) return;
    
    const w = container.clientWidth;
    const h = container.clientHeight;
    if (w === 0 || h === 0) return; // Skip resizing if container is hidden/collapsed
    
    threeCamera.aspect = w / h;
    threeCamera.updateProjectionMatrix();
    threeRenderer.setSize(w, h);
    renderScene();
}

function renderScene() {
    if (threeRenderer && threeScene && threeCamera) {
        threeRenderer.render(threeScene, threeCamera);
    }
}

function animateSceneDesigner() {
    requestAnimationFrame(animateSceneDesigner);
    if (threeOrbitControls) {
        threeOrbitControls.update();
    }
    renderScene();
}

function clearThreeSceneObjects() {
    threeObjects.forEach(obj => {
        threeScene.remove(obj.group || obj.mesh);
    });
    threeObjects = [];
    if (threeTransformControls) {
        threeTransformControls.detach();
    }
    if (boxHelper) {
        threeScene.remove(boxHelper);
        boxHelper = null;
    }
    selectedThreeObject = null;
    updatePropertiesPanel();
}

function buildThreeScene() {
    if (!threeScene) return;
    
    clearThreeSceneObjects();
    
    parsedSceneData.forEach(item => {
        if (item.type === 'comment' || item.type === 'blank' || item.type === 'unknown') {
            return;
        }
        
        let mesh = null;
        let group = null;
        
        if (item.type === 'grid') {
            const gridHelper = new THREE.GridHelper(item.size, item.size / item.spacing, getHexColor(item.color), getHexColor(item.color));
            gridHelper.position.set(item.x, 0, item.z);
            gridHelper.userData = { id: item.id };
            threeScene.add(gridHelper);
            
            threeObjects.push({
                dataId: item.id,
                mesh: gridHelper,
                group: null
            });
        } 
        else if (item.type === 'cube') {
            const geometry = new THREE.BoxGeometry(1, 1, 1);
            const material = getThreeMaterial(item.material, item.color);
            mesh = new THREE.Mesh(geometry, material);
            mesh.castShadow = true;
            mesh.receiveShadow = true;
            
            mesh.scale.set(item.sx, item.sy, item.sz);
            mesh.position.set(item.x, item.y, item.z);
            mesh.userData = { id: item.id };
            
            threeScene.add(mesh);
            
            threeObjects.push({
                dataId: item.id,
                mesh: mesh,
                group: null
            });
        } 
        else if (item.type === 'tree') {
            group = new THREE.Group();
            group.position.set(item.x, item.y, item.z);
            group.userData = { id: item.id };
            
            const trunkHeight = item.trunkHeight;
            const trunkGeom = new THREE.CylinderGeometry(0.15, 0.15, trunkHeight, 8);
            const trunkMat = getThreeMaterial('wood', 'saddlebrown');
            const trunkMesh = new THREE.Mesh(trunkGeom, trunkMat);
            trunkMesh.castShadow = true;
            trunkMesh.receiveShadow = true;
            trunkMesh.position.y = trunkHeight / 2;
            group.add(trunkMesh);
            
            const foliageSize = item.foliageSize;
            const foliageHeight = foliageSize * 1.5;
            const foliageGeom = new THREE.ConeGeometry(foliageSize / 2, foliageHeight, 8);
            const foliageMat = getThreeMaterial(item.material, item.color || 'forestgreen');
            const foliageMesh = new THREE.Mesh(foliageGeom, foliageMat);
            foliageMesh.castShadow = true;
            foliageMesh.receiveShadow = true;
            foliageMesh.position.y = trunkHeight + foliageHeight / 2;
            group.add(foliageMesh);
            
            threeScene.add(group);
            
            threeObjects.push({
                dataId: item.id,
                mesh: foliageMesh,
                group: group
            });
        } 
        else if (item.type === 'model') {
            group = new THREE.Group();
            group.position.set(item.x, item.y, item.z);
            group.rotation.set(item.rx, item.ry, item.rz);
            group.scale.set(item.sx, item.sy, item.sz);
            group.userData = { id: item.id };
            
            let geom;
            let mat = getThreeMaterial(item.material, item.color);
            let targetMesh;
            
            const lowerPath = item.filePath.toLowerCase();
            if (lowerPath.includes('rock')) {
                geom = new THREE.DodecahedronGeometry(0.5);
                const pos = geom.attributes.position;
                for (let i = 0; i < pos.count; i++) {
                    pos.setX(i, pos.getX(i) + (Math.random() - 0.5) * 0.08);
                    pos.setY(i, pos.getY(i) + (Math.random() - 0.5) * 0.08);
                    pos.setZ(i, pos.getZ(i) + (Math.random() - 0.5) * 0.08);
                }
                geom.computeVertexNormals();
                targetMesh = new THREE.Mesh(geom, mat);
                targetMesh.castShadow = true;
                targetMesh.receiveShadow = true;
                group.add(targetMesh);
            } 
            else if (lowerPath.includes('crate')) {
                geom = new THREE.BoxGeometry(1, 1, 1);
                targetMesh = new THREE.Mesh(geom, mat);
                targetMesh.castShadow = true;
                targetMesh.receiveShadow = true;
                group.add(targetMesh);
            } 
            else {
                geom = new THREE.CylinderGeometry(0.3, 0.3, 1, 8);
                targetMesh = new THREE.Mesh(geom, mat);
                targetMesh.castShadow = true;
                targetMesh.receiveShadow = true;
                group.add(targetMesh);
            }
            
            threeScene.add(group);
            
            threeObjects.push({
                dataId: item.id,
                mesh: targetMesh,
                group: group
            });
        }
    });
    
    updateExplorer();
}

function onViewportClick(event) {
    if (threeTransformControls.dragging) return;
    
    const moveX = Math.abs(event.clientX - pointerDownX);
    const moveY = Math.abs(event.clientY - pointerDownY);
    if (moveX > 5 || moveY > 5) return;
    
    const rect = threeRenderer.domElement.getBoundingClientRect();
    mousePosition.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    mousePosition.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    
    raycaster.setFromCamera(mousePosition, threeCamera);
    
    const meshesToIntersect = [];
    threeObjects.forEach(obj => {
        if (obj.group) {
            obj.group.traverse(child => {
                if (child.isMesh) meshesToIntersect.push(child);
            });
        } else if (obj.mesh) {
            meshesToIntersect.push(obj.mesh);
        }
    });
    
    const intersects = raycaster.intersectObjects(meshesToIntersect);
    
    if (intersects.length > 0) {
        const hitMesh = intersects[0].object;
        let matchedObj = null;
        for (const obj of threeObjects) {
            if (obj.mesh === hitMesh) {
                matchedObj = obj;
                break;
            }
            if (obj.group) {
                let isChild = false;
                obj.group.traverse(child => {
                    if (child === hitMesh) isChild = true;
                });
                if (isChild) {
                    matchedObj = obj;
                    break;
                }
            }
        }
        
        if (matchedObj) {
            selectObject(matchedObj);
            return;
        }
    } else {
        selectObject(null);
    }
}

function selectObject(obj) {
    if (boxHelper) {
        threeScene.remove(boxHelper);
        boxHelper = null;
    }
    
    selectedThreeObject = obj;
    
    if (obj) {
        const target = obj.group || obj.mesh;
        
        boxHelper = new THREE.BoxHelper(target, 0x00f2fe);
        threeScene.add(boxHelper);
        
        if (currentGizmoMode !== 'select') {
            threeTransformControls.attach(target);
            threeTransformControls.setMode(currentGizmoMode);
        } else {
            threeTransformControls.detach();
        }
    } else {
        threeTransformControls.detach();
    }
    
    highlightExplorerItem();
    updatePropertiesPanel();
}

function updateSelectedObjectPropertiesFromMesh() {
    if (!selectedThreeObject) return;
    
    const dataItem = parsedSceneData.find(d => d.id === selectedThreeObject.dataId);
    if (!dataItem) return;
    
    const target = selectedThreeObject.group || selectedThreeObject.mesh;
    
    dataItem.x = parseFloat(target.position.x.toFixed(3));
    if (dataItem.type !== 'grid') {
        dataItem.y = parseFloat(target.position.y.toFixed(3));
    }
    dataItem.z = parseFloat(target.position.z.toFixed(3));
    
    if (dataItem.type === 'cube') {
        dataItem.sx = parseFloat(target.scale.x.toFixed(3));
        dataItem.sy = parseFloat(target.scale.y.toFixed(3));
        dataItem.sz = parseFloat(target.scale.z.toFixed(3));
    } else if (dataItem.type === 'tree') {
        const scaleY = target.scale.y;
        const scaleX = target.scale.x;
        dataItem.trunkHeight = parseFloat((dataItem.trunkHeight * scaleY).toFixed(3));
        dataItem.foliageSize = parseFloat((dataItem.foliageSize * scaleX).toFixed(3));
        target.scale.set(1, 1, 1);
        rebuildSelectedTreeMesh(selectedThreeObject, dataItem);
    } else if (dataItem.type === 'model') {
        dataItem.sx = parseFloat(target.scale.x.toFixed(3));
        dataItem.sy = parseFloat(target.scale.y.toFixed(3));
        dataItem.sz = parseFloat(target.scale.z.toFixed(3));
        
        dataItem.rx = parseFloat(target.rotation.x.toFixed(3));
        dataItem.ry = parseFloat(target.rotation.y.toFixed(3));
        dataItem.rz = parseFloat(target.rotation.z.toFixed(3));
    }
    
    if (boxHelper) {
        boxHelper.update();
    }
    
    updatePropertiesPanel();
    syncDesignerToMonaco();
}

function rebuildSelectedTreeMesh(obj, dataItem) {
    while(obj.group.children.length > 0){
        obj.group.remove(obj.group.children[0]);
    }
    
    const trunkHeight = dataItem.trunkHeight;
    const trunkGeom = new THREE.CylinderGeometry(0.15, 0.15, trunkHeight, 8);
    const trunkMat = getThreeMaterial('wood', 'saddlebrown');
    const trunkMesh = new THREE.Mesh(trunkGeom, trunkMat);
    trunkMesh.castShadow = true;
    trunkMesh.receiveShadow = true;
    trunkMesh.position.y = trunkHeight / 2;
    obj.group.add(trunkMesh);
    
    const foliageSize = dataItem.foliageSize;
    const foliageHeight = foliageSize * 1.5;
    const foliageGeom = new THREE.ConeGeometry(foliageSize / 2, foliageHeight, 8);
    const foliageMat = getThreeMaterial(dataItem.material, dataItem.color || 'forestgreen');
    const foliageMesh = new THREE.Mesh(foliageGeom, foliageMat);
    foliageMesh.castShadow = true;
    foliageMesh.receiveShadow = true;
    foliageMesh.position.y = trunkHeight + foliageHeight / 2;
    obj.group.add(foliageMesh);
}

function syncDesignerToMonaco() {
    if (!activeTabPath || !editor) return;
    const tab = openTabs.find(t => t.path === activeTabPath);
    if (!tab) return;
    
    const newText = generateEpscene(parsedSceneData);
    const model = tab.model;
    
    if (model.getValue() !== newText) {
        model.pushEditOperations(
            [],
            [{
                range: model.getFullModelRange(),
                text: newText
            }],
            () => null
        );
        
        const currentVal = model.getValue();
        const isDirty = currentVal !== tab.originalContent;
        if (tab.isDirty !== isDirty) {
            tab.isDirty = isDirty;
            renderTabs();
        }
    }
}

function updateExplorer() {
    const explorerList = document.getElementById('scene-explorer-list');
    if (!explorerList) return;
    
    explorerList.innerHTML = '';
    
    parsedSceneData.forEach(item => {
        if (item.type === 'comment' || item.type === 'blank' || item.type === 'unknown') {
            return;
        }
        
        const itemEl = document.createElement('div');
        itemEl.className = 'explorer-tree-item';
        itemEl.dataset.id = item.id;
        
        if (selectedThreeObject && selectedThreeObject.dataId === item.id) {
            itemEl.classList.add('active-item');
        }
        
        let icon = 'box';
        let typeClass = 'item-type-cube';
        let displayName = item.name || 'Cube';
        
        if (item.type === 'grid') {
            icon = 'grid';
            typeClass = 'item-type-grid';
            displayName = `Grid (${item.size}x${item.spacing})`;
        } else if (item.type === 'tree') {
            icon = 'tree-pine';
            typeClass = 'item-type-tree';
            displayName = item.name || 'Tree';
        } else if (item.type === 'model') {
            icon = 'file-key';
            typeClass = 'item-type-model';
            displayName = item.name || 'Model';
        }
        
        itemEl.innerHTML = `
            <i data-lucide="${icon}"></i>
            <span class="explorer-tree-item-name">${displayName}</span>
        `;
        
        itemEl.addEventListener('click', () => {
            const match = threeObjects.find(o => o.dataId === item.id);
            if (match) {
                selectObject(match);
                const target = match.group || match.mesh;
                threeOrbitControls.target.copy(target.position);
            }
        });
        
        explorerList.appendChild(itemEl);
    });
    
    updateIcons();
}

function highlightExplorerItem() {
    document.querySelectorAll('.explorer-tree-item').forEach(itemEl => {
        const id = itemEl.dataset.id;
        if (selectedThreeObject && selectedThreeObject.dataId === id) {
            itemEl.classList.add('active-item');
            itemEl.scrollIntoView({ block: 'nearest' });
        } else {
            itemEl.classList.remove('active-item');
        }
    });
}

function updatePropertiesPanel() {
    const panel = document.getElementById('scene-properties-panel');
    if (!panel) return;
    
    if (!selectedThreeObject) {
        panel.innerHTML = `
            <div class="empty-selection-message">
                Select an object to edit properties
            </div>
        `;
        return;
    }
    
    const dataItem = parsedSceneData.find(d => d.id === selectedThreeObject.dataId);
    if (!dataItem) {
        panel.innerHTML = `<div class="empty-selection-message">Object data not found</div>`;
        return;
    }
    
    let propertiesHtml = '';
    
    propertiesHtml += `
        <div class="properties-section">
            <span class="properties-section-title">Identity</span>
            <div class="property-row">
                <span class="property-label">Class</span>
                <span class="property-value" style="font-weight:700; color:var(--accent-cyan); font-family:var(--font-mono); font-size:11px;">
                    ${dataItem.type.toUpperCase()}
                </span>
            </div>
    `;
    
    if (dataItem.type !== 'grid') {
        propertiesHtml += `
            <div class="property-row">
                <span class="property-label">Name</span>
                <div class="property-value">
                    <input type="text" class="property-input" id="prop-name" value="${dataItem.name || ''}">
                </div>
            </div>
        `;
    }
    propertiesHtml += `</div>`;
    
    propertiesHtml += `
        <div class="properties-section">
            <span class="properties-section-title">Transform</span>
            <div class="property-row">
                <span class="property-label">Position</span>
                <div class="property-value">
                    <div class="property-coord-group">
                        <div class="coord-wrapper">
                            <span class="coord-label">X</span>
                            <input type="number" step="0.1" class="property-input-coord" id="prop-pos-x" value="${dataItem.x}">
                        </div>
                        ${dataItem.type !== 'grid' ? `
                        <div class="coord-wrapper">
                            <span class="coord-label">Y</span>
                            <input type="number" step="0.1" class="property-input-coord" id="prop-pos-y" value="${dataItem.y}">
                        </div>
                        ` : ''}
                        <div class="coord-wrapper">
                            <span class="coord-label">Z</span>
                            <input type="number" step="0.1" class="property-input-coord" id="prop-pos-z" value="${dataItem.z !== undefined ? dataItem.z : 0}">
                        </div>
                    </div>
                </div>
            </div>
    `;
    
    if (dataItem.type === 'cube') {
        propertiesHtml += `
            <div class="property-row">
                <span class="property-label">Size</span>
                <div class="property-value">
                    <div class="property-coord-group">
                        <div class="coord-wrapper">
                            <span class="coord-label">X</span>
                            <input type="number" step="0.1" min="0.1" class="property-input-coord" id="prop-scale-x" value="${dataItem.sx}">
                        </div>
                        <div class="coord-wrapper">
                            <span class="coord-label">Y</span>
                            <input type="number" step="0.1" min="0.1" class="property-input-coord" id="prop-scale-y" value="${dataItem.sy}">
                        </div>
                        <div class="coord-wrapper">
                            <span class="coord-label">Z</span>
                            <input type="number" step="0.1" min="0.1" class="property-input-coord" id="prop-scale-z" value="${dataItem.sz}">
                        </div>
                    </div>
                </div>
            </div>
        `;
    } else if (dataItem.type === 'tree') {
        propertiesHtml += `
            <div class="property-row">
                <span class="property-label">Trunk Ht</span>
                <div class="property-value">
                    <input type="number" step="0.1" min="0.1" class="property-input" id="prop-tree-th" value="${dataItem.trunkHeight}">
                </div>
            </div>
            <div class="property-row">
                <span class="property-label">Foliage Sz</span>
                <div class="property-value">
                    <input type="number" step="0.1" min="0.1" class="property-input" id="prop-tree-fs" value="${dataItem.foliageSize}">
                </div>
            </div>
        `;
    } else if (dataItem.type === 'model') {
        propertiesHtml += `
            <div class="property-row">
                <span class="property-label">Scale</span>
                <div class="property-value">
                    <div class="property-coord-group">
                        <div class="coord-wrapper">
                            <span class="coord-label">X</span>
                            <input type="number" step="0.1" min="0.1" class="property-input-coord" id="prop-scale-x" value="${dataItem.sx}">
                        </div>
                        <div class="coord-wrapper">
                            <span class="coord-label">Y</span>
                            <input type="number" step="0.1" min="0.1" class="property-input-coord" id="prop-scale-y" value="${dataItem.sy}">
                        </div>
                        <div class="coord-wrapper">
                            <span class="coord-label">Z</span>
                            <input type="number" step="0.1" min="0.1" class="property-input-coord" id="prop-scale-z" value="${dataItem.sz}">
                        </div>
                    </div>
                </div>
            </div>
            <div class="property-row">
                <span class="property-label">Rotation</span>
                <div class="property-value">
                    <div class="property-coord-group">
                        <div class="coord-wrapper">
                            <span class="coord-label">X°</span>
                            <input type="number" step="1" class="property-input-coord" id="prop-rot-x" value="${Math.round(dataItem.rx * 180 / Math.PI)}">
                        </div>
                        <div class="coord-wrapper">
                            <span class="coord-label">Y°</span>
                            <input type="number" step="1" class="property-input-coord" id="prop-rot-y" value="${Math.round(dataItem.ry * 180 / Math.PI)}">
                        </div>
                        <div class="coord-wrapper">
                            <span class="coord-label">Z°</span>
                            <input type="number" step="1" class="property-input-coord" id="prop-rot-z" value="${Math.round(dataItem.rz * 180 / Math.PI)}">
                        </div>
                    </div>
                </div>
            </div>
        `;
    } else if (dataItem.type === 'grid') {
        propertiesHtml += `
            <div class="property-row">
                <span class="property-label">Grid Size</span>
                <div class="property-value">
                    <input type="number" step="1" min="1" class="property-input" id="prop-grid-size" value="${dataItem.size}">
                </div>
            </div>
            <div class="property-row">
                <span class="property-label">Spacing</span>
                <div class="property-value">
                    <input type="number" step="1" min="1" class="property-input" id="prop-grid-spacing" value="${dataItem.spacing}">
                </div>
            </div>
        `;
    }
    
    propertiesHtml += `</div>`;
    
    if (dataItem.type !== 'grid') {
        const materialOptions = ['Plastic', 'Neon', 'Metal', 'Glass', 'Wood'].map(opt => 
            `<option value="${opt}" ${dataItem.material === opt ? 'selected' : ''}>${opt}</option>`
        ).join('');
        
        const colorOptions = ['white', 'red', 'green', 'blue', 'yellow', 'purple', 'orange', 'cyan', 'magenta', 'gray', 'lightgray', 'darkgray', 'brown', 'saddlebrown', 'forestgreen', 'black'].map(opt => 
            `<option value="${opt}" ${dataItem.color.toLowerCase() === opt ? 'selected' : ''}>${opt}</option>`
        ).join('');
        
        propertiesHtml += `
            <div class="properties-section">
                <span class="properties-section-title">Appearance</span>
                <div class="property-row">
                    <span class="property-label">Color</span>
                    <div class="property-value">
                        <select class="property-select" id="prop-color">
                            ${colorOptions}
                        </select>
                    </div>
                </div>
                <div class="property-row">
                    <span class="property-label">Material</span>
                    <div class="property-value">
                        <select class="property-select" id="prop-material">
                            ${materialOptions}
                        </select>
                    </div>
                </div>
            </div>
        `;
    } else {
        const colorOptions = ['gray', 'lightgray', 'darkgray', 'white', 'red', 'green', 'blue', 'yellow', 'purple', 'black'].map(opt => 
            `<option value="${opt}" ${dataItem.color.toLowerCase() === opt ? 'selected' : ''}>${opt}</option>`
        ).join('');
        propertiesHtml += `
            <div class="properties-section">
                <span class="properties-section-title">Appearance</span>
                <div class="property-row">
                    <span class="property-label">Color</span>
                    <div class="property-value">
                        <select class="property-select" id="prop-color">
                            ${colorOptions}
                        </select>
                    </div>
                </div>
            </div>
        `;
    }
    
    if (dataItem.type !== 'grid') {
        propertiesHtml += `
            <div class="properties-section">
                <span class="properties-section-title">Behavior</span>
                <div class="property-row">
                    <span class="property-label">Anchored</span>
                    <div class="property-value">
                        <input type="checkbox" class="property-checkbox" id="prop-anchored" ${dataItem.anchored ? 'checked' : ''}>
                    </div>
                </div>
                <div class="property-row">
                    <span class="property-label">CanCollide</span>
                    <div class="property-value">
                        <input type="checkbox" class="property-checkbox" id="prop-cancollide" ${dataItem.canCollide ? 'checked' : ''}>
                    </div>
                </div>
            </div>
        `;
    }
    
    if (dataItem.type === 'model') {
        propertiesHtml += `
            <div class="properties-section">
                <span class="properties-section-title">Asset</span>
                <div class="property-row">
                    <span class="property-label">File Path</span>
                    <div class="property-value">
                        <select class="property-select" id="prop-filepath">
                            <option value="assets/rock.obj" ${dataItem.filePath === 'assets/rock.obj' ? 'selected' : ''}>assets/rock.obj</option>
                            <option value="assets/crate.obj" ${dataItem.filePath === 'assets/crate.obj' ? 'selected' : ''}>assets/crate.obj</option>
                            <option value="assets/tree.obj" ${dataItem.filePath === 'assets/tree.obj' ? 'selected' : ''}>assets/tree.obj</option>
                        </select>
                    </div>
                </div>
            </div>
        `;
    }
    
    panel.innerHTML = `<div class="properties-list">${propertiesHtml}</div>`;
    setupPropertyChangeListeners(dataItem);
}

function setupPropertyChangeListeners(dataItem) {
    const updateSceneAndSync = () => {
        buildThreeScene();
        const updatedSelected = threeObjects.find(o => o.dataId === dataItem.id);
        if (updatedSelected) {
            selectObject(updatedSelected);
        }
        syncDesignerToMonaco();
    };
    
    const bindInput = (id, field, parser = parseFloat) => {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('input', (e) => {
                const val = parser(e.target.value);
                if (field === 'name') {
                    dataItem[field] = e.target.value;
                    syncDesignerToMonaco();
                    updateExplorer();
                    return;
                }
                if (!isNaN(val)) {
                    dataItem[field] = val;
                    const target = selectedThreeObject.group || selectedThreeObject.mesh;
                    if (field === 'x') target.position.x = val;
                    else if (field === 'y') target.position.y = val;
                    else if (field === 'z') target.position.z = val;
                    else if (field === 'sx') target.scale.x = val;
                    else if (field === 'sy') target.scale.y = val;
                    else if (field === 'sz') target.scale.z = val;
                    
                    if (boxHelper) boxHelper.update();
                    syncDesignerToMonaco();
                }
            });
            el.addEventListener('change', updateSceneAndSync);
        }
    };
    
    const bindSelect = (id, field) => {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('change', (e) => {
                dataItem[field] = e.target.value;
                updateSceneAndSync();
            });
        }
    };
    
    const bindCheckbox = (id, field) => {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('change', (e) => {
                dataItem[field] = e.target.checked;
                updateSceneAndSync();
            });
        }
    };
    
    bindInput('prop-name', 'name', String);
    bindInput('prop-pos-x', 'x');
    bindInput('prop-pos-y', 'y');
    bindInput('prop-pos-z', 'z');
    
    bindInput('prop-scale-x', 'sx');
    bindInput('prop-scale-y', 'sy');
    bindInput('prop-scale-z', 'sz');
    
    bindInput('prop-tree-th', 'trunkHeight');
    bindInput('prop-tree-fs', 'foliageSize');
    bindInput('prop-grid-size', 'size');
    bindInput('prop-grid-spacing', 'spacing');
    
    ['x', 'y', 'z'].forEach(axis => {
        const el = document.getElementById(`prop-rot-${axis}`);
        if (el) {
            el.addEventListener('input', (e) => {
                const deg = parseFloat(e.target.value);
                if (!isNaN(deg)) {
                    dataItem[`r${axis}`] = deg * Math.PI / 180;
                    const target = selectedThreeObject.group || selectedThreeObject.mesh;
                    target.rotation[axis] = dataItem[`r${axis}`];
                    if (boxHelper) boxHelper.update();
                    syncDesignerToMonaco();
                }
            });
            el.addEventListener('change', updateSceneAndSync);
        }
    });
    
    bindSelect('prop-color', 'color');
    bindSelect('prop-material', 'material');
    bindSelect('prop-filepath', 'filePath');
    
    bindCheckbox('prop-anchored', 'anchored');
    bindCheckbox('prop-cancollide', 'canCollide');
}

function onDesignerKeyDown(event) {
    if (designerMode !== 'scene' || !activeTabPath || !activeTabPath.endsWith('.epscene')) return;
    
    if (document.activeElement && (document.activeElement.tagName === 'INPUT' || document.activeElement.tagName === 'SELECT')) {
        return;
    }
    
    const key = event.key.toLowerCase();
    
    if (key === 'q') {
        setGizmoMode('select');
    } else if (key === 'w') {
        setGizmoMode('translate');
    } else if (key === 'e') {
        setGizmoMode('scale');
    } else if (key === 'r') {
        setGizmoMode('rotate');
    } else if (key === 'delete' || key === 'backspace') {
        deleteSelectedObject();
    } else if (event.ctrlKey && key === 'd') {
        event.preventDefault();
        duplicateSelectedObject();
    }
}

function setGizmoMode(mode) {
    currentGizmoMode = mode;
    
    ['select', 'translate', 'scale', 'rotate'].forEach(m => {
        const btn = document.getElementById(`tool-${m}`);
        if (btn) {
            if (m === mode) btn.classList.add('active');
            else btn.classList.remove('active');
        }
    });
    
    if (selectedThreeObject) {
        const target = selectedThreeObject.group || selectedThreeObject.mesh;
        if (mode === 'select') {
            threeTransformControls.detach();
        } else {
            threeTransformControls.attach(target);
            threeTransformControls.setMode(mode);
        }
    }
}

function addCubeObject() {
    const id = 'obj_' + Date.now() + '_' + Math.random().toString(36).substr(2, 4);
    const newCube = {
        type: 'cube',
        x: 0, y: 0.5, z: 0,
        sx: 1, sy: 1, sz: 1,
        color: 'white',
        material: 'Plastic',
        anchored: true,
        canCollide: true,
        name: 'BlockPart',
        id: id
    };
    
    parsedSceneData.push(newCube);
    buildThreeScene();
    
    const newMeshObj = threeObjects.find(o => o.dataId === id);
    if (newMeshObj) {
        selectObject(newMeshObj);
    }
    
    syncDesignerToMonaco();
}

function addTreeObject() {
    const id = 'obj_' + Date.now() + '_' + Math.random().toString(36).substr(2, 4);
    const newTree = {
        type: 'tree',
        x: 0, y: 0, z: 0,
        trunkHeight: 2,
        foliageSize: 1.5,
        material: 'Plastic',
        anchored: true,
        canCollide: true,
        name: 'TreePart',
        id: id
    };
    
    parsedSceneData.push(newTree);
    buildThreeScene();
    
    const newMeshObj = threeObjects.find(o => o.dataId === id);
    if (newMeshObj) {
        selectObject(newMeshObj);
    }
    
    syncDesignerToMonaco();
}

function addModelObject() {
    const id = 'obj_' + Date.now() + '_' + Math.random().toString(36).substr(2, 4);
    const newModel = {
        type: 'model',
        x: 0, y: 0.5, z: 0,
        sx: 1, sy: 1, sz: 1,
        rx: 0, ry: 0, rz: 0,
        filePath: 'assets/rock.obj',
        color: 'yellow',
        material: 'Metal',
        anchored: true,
        canCollide: true,
        name: 'MeshPart',
        id: id
    };
    
    parsedSceneData.push(newModel);
    buildThreeScene();
    
    const newMeshObj = threeObjects.find(o => o.dataId === id);
    if (newMeshObj) {
        selectObject(newMeshObj);
    }
    
    syncDesignerToMonaco();
}

function addGridObject() {
    const id = 'obj_' + Date.now() + '_' + Math.random().toString(36).substr(2, 4);
    const newGrid = {
        type: 'grid',
        x: 0, z: 0,
        size: 30,
        spacing: 2,
        color: 'gray',
        id: id
    };
    
    parsedSceneData.push(newGrid);
    buildThreeScene();
    
    const newMeshObj = threeObjects.find(o => o.dataId === id);
    if (newMeshObj) {
        selectObject(newMeshObj);
    }
    
    syncDesignerToMonaco();
}

function duplicateSelectedObject() {
    if (!selectedThreeObject) return;
    
    const sourceItem = parsedSceneData.find(d => d.id === selectedThreeObject.dataId);
    if (!sourceItem) return;
    
    const id = 'obj_' + Date.now() + '_' + Math.random().toString(36).substr(2, 4);
    const clone = JSON.parse(JSON.stringify(sourceItem));
    clone.id = id;
    clone.x += 2;
    if (clone.name) {
        clone.name += '_Copy';
    }
    
    parsedSceneData.push(clone);
    buildThreeScene();
    
    const newMeshObj = threeObjects.find(o => o.dataId === id);
    if (newMeshObj) {
        selectObject(newMeshObj);
    }
    
    syncDesignerToMonaco();
    showToast(`Duplicated ${clone.name || 'Object'}`, 'success');
}

function deleteSelectedObject() {
    if (!selectedThreeObject) return;
    
    const id = selectedThreeObject.dataId;
    const index = parsedSceneData.findIndex(d => d.id === id);
    
    if (index !== -1) {
        const deletedName = parsedSceneData[index].name || parsedSceneData[index].type;
        parsedSceneData.splice(index, 1);
        
        threeTransformControls.detach();
        if (boxHelper) {
            threeScene.remove(boxHelper);
            boxHelper = null;
        }
        
        selectedThreeObject = null;
        buildThreeScene();
        syncDesignerToMonaco();
        
        showToast(`Deleted ${deletedName}`, 'info');
    }
}

function initToolbarBindings() {
    document.getElementById('tool-select').addEventListener('click', () => setGizmoMode('select'));
    document.getElementById('tool-translate').addEventListener('click', () => setGizmoMode('translate'));
    document.getElementById('tool-scale').addEventListener('click', () => setGizmoMode('scale'));
    document.getElementById('tool-rotate').addEventListener('click', () => setGizmoMode('rotate'));
    
    document.getElementById('add-cube').addEventListener('click', addCubeObject);
    document.getElementById('add-tree').addEventListener('click', addTreeObject);
    document.getElementById('add-model').addEventListener('click', addModelObject);
    document.getElementById('add-grid').addEventListener('click', addGridObject);
    
    document.getElementById('btn-duplicate').addEventListener('click', duplicateSelectedObject);
    document.getElementById('btn-delete').addEventListener('click', deleteSelectedObject);
    
    const canvasEl = threeRenderer.domElement;
    canvasEl.addEventListener('pointerdown', (e) => {
        pointerDownX = e.clientX;
        pointerDownY = e.clientY;
    });
    canvasEl.addEventListener('pointerup', onViewportClick);
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
    
    document.getElementById('empty-editor-message').style.display = 'none';
    
    const isEpscene = filePath.endsWith('.epscene');
    const toggleContainer = document.getElementById('editor-mode-toggle');
    
    if (isEpscene) {
        toggleContainer.style.display = 'flex';
        editor.setModel(tab.model);
        
        const fileContent = tab.model.getValue();
        parsedSceneData = parseEpscene(fileContent);
        
        if (designerMode === 'scene') {
            document.getElementById('monaco-editor-instance').style.display = 'none';
            document.getElementById('scene-designer-pane').style.display = 'block';
            initSceneDesigner();
            buildThreeScene();
        } else {
            document.getElementById('monaco-editor-instance').style.display = 'block';
            document.getElementById('scene-designer-pane').style.display = 'none';
            editor.focus();
            setTimeout(() => editor.layout(), 50);
        }
    } else {
        toggleContainer.style.display = 'none';
        document.getElementById('scene-designer-pane').style.display = 'none';
        document.getElementById('monaco-editor-instance').style.display = 'block';
        editor.setModel(tab.model);
        editor.focus();
        setTimeout(() => editor.layout(), 50);
    }

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
    if (tab.model) {
        tab.model.dispose();
    }
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

    // 12. Editor Mode toggling clicks
    document.getElementById('btn-mode-scene').addEventListener('click', () => {
        if (designerMode === 'scene') return;
        designerMode = 'scene';
        document.getElementById('btn-mode-scene').classList.add('active');
        document.getElementById('btn-mode-code').classList.remove('active');
        
        if (activeTabPath) {
            const currentText = editor.getValue();
            parsedSceneData = parseEpscene(currentText);
        }
        
        document.getElementById('monaco-editor-instance').style.display = 'none';
        document.getElementById('scene-designer-pane').style.display = 'block';
        initSceneDesigner();
        buildThreeScene();
    });
    
    document.getElementById('btn-mode-code').addEventListener('click', () => {
        if (designerMode === 'code') return;
        designerMode = 'code';
        document.getElementById('btn-mode-scene').classList.remove('active');
        document.getElementById('btn-mode-code').classList.add('active');
        
        document.getElementById('scene-designer-pane').style.display = 'none';
        document.getElementById('monaco-editor-instance').style.display = 'block';
        if (editor) {
            editor.layout();
            editor.focus();
        }
    });

    // 14. Start connection heartbeat checks to auto-close when server shuts down (Ctrl+C)
    startHeartbeat();
});

// Heartbeat to check if server goes down and auto-close page
function startHeartbeat() {
    setInterval(async () => {
        try {
            const response = await fetch('/api/ping');
            if (!response.ok) throw new Error();
        } catch (err) {
            // Attempt to close the window/tab
            window.open('', '_self', '');
            window.close();
            
            // Render beautiful disconnected overlay in case browser blocks window.close()
            showDisconnectedOverlay();
        }
    }, 1500);
}

function showDisconnectedOverlay() {
    if (document.getElementById('disconnected-overlay')) return;
    
    const overlay = document.createElement('div');
    overlay.id = 'disconnected-overlay';
    overlay.style.position = 'fixed';
    overlay.style.top = '0';
    overlay.style.left = '0';
    overlay.style.width = '100vw';
    overlay.style.height = '100vh';
    overlay.style.background = 'rgba(15, 23, 42, 0.95)';
    overlay.style.backdropFilter = 'blur(10px)';
    overlay.style.zIndex = '99999';
    overlay.style.display = 'flex';
    overlay.style.flexDirection = 'column';
    overlay.style.justifyContent = 'center';
    overlay.style.alignItems = 'center';
    overlay.style.color = '#f8fafc';
    overlay.style.fontFamily = "'Outfit', system-ui, sans-serif";
    
    overlay.innerHTML = `
        <div style="text-align: center; max-width: 400px; padding: 2.5rem; border-radius: 16px; background: #1e293b; border: 1px solid #334155; box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5);">
            <div style="font-size: 3.5rem; margin-bottom: 1rem;">⚡</div>
            <h2 style="font-size: 1.5rem; margin-bottom: 0.5rem; font-weight: 700; background: linear-gradient(135deg, #f87171, #ef4444); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">IDE Session Closed</h2>
            <p style="color: #94a3b8; font-size: 0.95rem; line-height: 1.5; margin-bottom: 2rem;">
                The server connection was lost (e.g. server shut down or Ctrl+C). You can safely close this tab now.
            </p>
            <button onclick="window.location.reload()" style="background: #3b82f6; hover:background: #2563eb; color: white; border: none; padding: 0.75rem 1.75rem; border-radius: 8px; font-weight: 600; cursor: pointer; transition: all 0.2s ease;">
                Retry Connecting
            </button>
        </div>
    `;
    document.body.appendChild(overlay);
}

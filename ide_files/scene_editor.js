// 3D Scene Editor using Three.js, OrbitControls, and TransformControls
(function() {
    let scene, camera, renderer, orbitControls, transformControls;
    let gridHelper;
    let selectedObject = null;
    let objects = []; // List of active meshes/groups in scene
    let viewportDiv = null;
    let snapEnabled = true;
    let snapValue = 0.5;
    let objLoader = null;

    // Default values for new spawns
    const defaultColor = 'red';

    // Color conversion map to match EpilepsyLang color strings to CSS/Hex colors
    const colorMap = {
        'black': '#000000',
        'white': '#ffffff',
        'red': '#ff0000',
        'green': '#00ff00',
        'blue': '#0000ff',
        'yellow': '#ffff00',
        'purple': '#800080',
        'orange': '#ffa500',
        'cyan': '#00ffff',
        'magenta': '#ff00ff',
        'gray': '#808080',
        'lightgray': '#d3d3d3',
        'darkgray': '#a9a9a9',
        'brown': '#8b4513',
        'saddlebrown': '#8b4513',
        'forestgreen': '#228b22'
    };

    function hexFromColor(colorName) {
        if (colorName.startsWith('#')) return colorName;
        return colorMap[colorName.toLowerCase()] || '#ffffff';
    }

    // Initialize 3D Viewport
    function init() {
        viewportDiv = document.getElementById('scene-viewport');
        if (!viewportDiv) return;

        // 1. Scene
        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x0a0f1d);

        // 2. Camera
        camera = new THREE.PerspectiveCamera(50, viewportDiv.clientWidth / viewportDiv.clientHeight, 0.1, 1000);
        camera.position.set(0, 5, 10);

        // 3. Renderer
        renderer = new THREE.WebGLRenderer({ antialias: true });
        renderer.setSize(viewportDiv.clientWidth, viewportDiv.clientHeight);
        renderer.shadowMap.enabled = true;
        viewportDiv.appendChild(renderer.domElement);

        // 4. Lights
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
        scene.add(ambientLight);

        const dirLight = new THREE.DirectionalLight(0xffffff, 0.8);
        dirLight.position.set(10, 20, 15);
        dirLight.castShadow = true;
        scene.add(dirLight);

        const pointLight = new THREE.PointLight(0x00f2fe, 0.5, 30);
        pointLight.position.set(0, 4, 0);
        scene.add(pointLight);

        // 5. Grid Helper
        gridHelper = new THREE.GridHelper(30, 60, 0x00f2fe, 0x243b55);
        gridHelper.position.y = 0;
        scene.add(gridHelper);

        // 6. Loader
        objLoader = new THREE.OBJLoader();

        // 7. Controls
        orbitControls = new THREE.OrbitControls(camera, renderer.domElement);
        orbitControls.enableDamping = true;
        orbitControls.dampingFactor = 0.05;
        orbitControls.maxPolarAngle = Math.PI / 2 - 0.01; // don't go under floor

        transformControls = new THREE.TransformControls(camera, renderer.domElement);
        transformControls.setSize(0.75);
        scene.add(transformControls);

        // Prevent OrbitControls camera movement when dragging transform arrows
        transformControls.addEventListener('dragging-changed', function(event) {
            orbitControls.enabled = !event.value;
        });

        // Event listener: update sidebar inputs when manual dragging is done
        transformControls.addEventListener('change', () => {
            if (selectedObject) {
                updatePropertiesPanel();
                if (window.markSceneTabDirty) window.markSceneTabDirty();
            }
        });

        // 8. Raycast for Selection clicking
        const raycaster = new THREE.Raycaster();
        const mouse = new THREE.Vector2();

        renderer.domElement.addEventListener('pointerdown', (e) => {
            // Only select on left click and when not clicking on the gizmo
            if (e.button !== 0 || transformControls.dragging) return;

            // Get viewport coordinates
            const rect = renderer.domElement.getBoundingClientRect();
            mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

            raycaster.setFromCamera(mouse, camera);
            
            // Collect list of clickable meshes (unpack groups)
            const clickables = [];
            objects.forEach(obj => {
                if (obj.isGroup) {
                    // Clicking any child selects the group
                    obj.traverse(child => {
                        if (child.isMesh) {
                            child.userData.groupRoot = obj;
                            clickables.push(child);
                        }
                    });
                } else {
                    clickables.push(obj);
                }
            });

            const intersects = raycaster.intersectObjects(clickables);
            if (intersects.length > 0) {
                const hit = intersects[0].object;
                const target = hit.userData.groupRoot || hit;
                selectObject(target);
            } else {
                // Clicked in empty space - deselect if transform gizmo isn't being clicked
                if (transformControls.axis === null) {
                    selectObject(null);
                }
            }
        });

        // 9. Toolbar Interactions
        document.getElementById('btn-gizmo-translate').onclick = () => setGizmoMode('translate');
        document.getElementById('btn-gizmo-rotate').onclick = () => setGizmoMode('rotate');
        document.getElementById('btn-gizmo-scale').onclick = () => setGizmoMode('scale');
        document.getElementById('btn-add-box').onclick = () => spawnBox();
        document.getElementById('btn-add-tree').onclick = () => spawnTree();
        document.getElementById('btn-clear-scene').onclick = () => confirmClearScene();

        // Snap Settings
        const chkSnap = document.getElementById('chk-grid-snap');
        const numSnap = document.getElementById('num-snap-val');
        chkSnap.onchange = () => {
            snapEnabled = chkSnap.checked;
            updateSnapSettings();
        };
        numSnap.onchange = () => {
            snapValue = parseFloat(numSnap.value) || 0.5;
            updateSnapSettings();
        };
        updateSnapSettings();

        // Snap shortcuts and key bindings
        window.addEventListener('keydown', handleKeyPress);

        // Assets Shelf inserts
        document.querySelectorAll('.asset-card[data-asset]').forEach(btn => {
            btn.onclick = () => spawnModelAsset(btn.dataset.asset);
        });
        document.getElementById('btn-import-custom').onclick = () => promptImportCustomModel();

        // Start Loop
        animate();
    }

    function animate() {
        requestAnimationFrame(animate);
        orbitControls.update();
        renderer.render(scene, camera);
    }

    // Adjust canvas layout on sidebar resize / panel change
    function resize() {
        if (!renderer || !camera || !viewportDiv) return;
        renderer.setSize(viewportDiv.clientWidth, viewportDiv.clientHeight);
        camera.aspect = viewportDiv.clientWidth / viewportDiv.clientHeight;
        camera.updateProjectionMatrix();
    }

    // Snapping configuration
    function updateSnapSettings() {
        if (snapEnabled) {
            transformControls.setTranslationSnap(snapValue);
            transformControls.setRotationSnap(THREE.MathUtils.degToRad(15)); // 15 degrees
            transformControls.setScaleSnap(snapValue);
        } else {
            transformControls.setTranslationSnap(null);
            transformControls.setRotationSnap(null);
            transformControls.setScaleSnap(null);
        }
    }

    // Set gizmo handle mode
    function setGizmoMode(mode) {
        transformControls.setMode(mode);
        document.querySelectorAll('.btn-group button[id^="btn-gizmo-"]').forEach(btn => {
            btn.classList.remove('active');
        });
        if (mode === 'translate') document.getElementById('btn-gizmo-translate').classList.add('active');
        if (mode === 'rotate') document.getElementById('btn-gizmo-rotate').classList.add('active');
        if (mode === 'scale') document.getElementById('btn-gizmo-scale').classList.add('active');
    }

    // Keyboard controls
    function handleKeyPress(e) {
        const activeEl = document.activeElement;
        // Don't capture keys if typing in properties sidebar input text fields
        if (activeEl && (activeEl.tagName === 'INPUT' || activeEl.tagName === 'SELECT')) return;

        // Verify that the Scene Editor is currently visible
        const editorRoot = document.getElementById('scene-editor-container');
        if (!editorRoot || editorRoot.style.display === 'none') return;

        if (e.key === 'w' || e.key === 'W') setGizmoMode('translate');
        if (e.key === 'e' || e.key === 'E') setGizmoMode('rotate');
        if (e.key === 'r' || e.key === 'R') setGizmoMode('scale');
        
        if (e.key === 'Delete' || e.key === 'Backspace') {
            deleteSelected();
        }
        
        if (e.ctrlKey && (e.key === 'd' || e.key === 'D')) {
            e.preventDefault();
            duplicateSelected();
        }

        if (e.key === 'Escape') {
            selectObject(null);
        }
    }

    // ----------------------------------------------------
    // Spawners / Add actions
    // ----------------------------------------------------

    // 1. Spawning a 3D Box
    function spawnBox(x = 0, y = 0.5, z = 0, sx = 1, sy = 1, sz = 1, color = defaultColor) {
        const geometry = new THREE.BoxGeometry(1, 1, 1);
        const material = new THREE.MeshStandardMaterial({
            color: new THREE.Color(hexFromColor(color)),
            roughness: 0.4,
            metalness: 0.1
        });
        const mesh = new THREE.Mesh(geometry, material);
        mesh.position.set(x, y, z);
        mesh.scale.set(sx, sy, sz);
        mesh.castShadow = true;
        mesh.receiveShadow = true;

        mesh.userData = {
            type: 'cube',
            color: color
        };

        scene.add(mesh);
        objects.push(mesh);
        selectObject(mesh);
        if (window.markSceneTabDirty) window.markSceneTabDirty();
        return mesh;
    }

    // 2. Spawning a low-poly tree structure (Group containing trunk Cylinder + foliage Cone)
    function spawnTree(x = 0, y = 0, z = 0, trunkHeight = 1.5, foliageSize = 2.0) {
        const group = new THREE.Group();
        group.position.set(x, y, z);

        // Trunk
        const trunkGeo = new THREE.CylinderGeometry(0.15, 0.15, trunkHeight, 6);
        const trunkMat = new THREE.MeshStandardMaterial({ color: 0x8b4513, roughness: 0.8 });
        const trunkMesh = new THREE.Mesh(trunkGeo, trunkMat);
        trunkMesh.position.y = trunkHeight / 2; // base on ground
        trunkMesh.castShadow = true;
        trunkMesh.receiveShadow = true;
        group.add(trunkMesh);

        // Foliage
        const leavesGeo = new THREE.ConeGeometry(foliageSize / 2, foliageSize * 1.5, 6);
        const leavesMat = new THREE.MeshStandardMaterial({ color: 0x228b22, roughness: 0.6 });
        const leavesMesh = new THREE.Mesh(leavesGeo, leavesMat);
        leavesMesh.position.y = trunkHeight + (foliageSize * 1.5) / 2;
        leavesMesh.castShadow = true;
        leavesMesh.receiveShadow = true;
        group.add(leavesMesh);

        group.userData = {
            type: 'tree',
            trunkHeight: trunkHeight,
            foliageSize: foliageSize
        };

        scene.add(group);
        objects.push(group);
        selectObject(group);
        if (window.markSceneTabDirty) window.markSceneTabDirty();
        return group;
    }

    // 3. Spawning custom model files
    function spawnModelAsset(objPath, x = 0, y = 0, z = 0, sx = 1, sy = 1, sz = 1, rx = 0, ry = 0, rz = 0, color = 'white') {
        objLoader.load('/' + objPath, (loadedObj) => {
            // Apply standard materials
            const hex = hexFromColor(color);
            const material = new THREE.MeshStandardMaterial({
                color: new THREE.Color(hex),
                roughness: 0.5,
                metalness: 0.1
            });

            loadedObj.traverse(child => {
                if (child.isMesh) {
                    child.material = material;
                    child.castShadow = true;
                    child.receiveShadow = true;
                }
            });

            loadedObj.position.set(x, y, z);
            loadedObj.scale.set(sx, sy, sz);
            loadedObj.rotation.set(rx, ry, rz);
            
            loadedObj.userData = {
                type: 'model',
                modelPath: objPath,
                color: color
            };

            scene.add(loadedObj);
            objects.push(loadedObj);
            selectObject(loadedObj);
            if (window.markSceneTabDirty) window.markSceneTabDirty();
        }, undefined, (error) => {
            console.error('Failed to load asset model:', objPath, error);
            if (window.showToast) window.showToast(`Error loading model: ${objPath}`, 'error');
        });
    }

    // 4. Import custom Blender model path dialog
    function promptImportCustomModel() {
        if (!window.showPromptModal) return;
        window.showPromptModal('Import Custom OBJ Model', 'Relative model path (e.g. assets/my_mesh.obj):', 'assets/my_mesh.obj', 'assets/', (path) => {
            // Verify path contains extension
            if (!path.toLowerCase().endsWith('.obj')) {
                throw new Error('Only standard wave-front (.obj) models are supported.');
            }
            spawnModelAsset(path);
            if (window.showToast) window.showToast(`Imported model successfully`, 'success');
        });
    }

    // ----------------------------------------------------
    // Operations & Editing actions
    // ----------------------------------------------------

    function selectObject(obj) {
        selectedObject = obj;
        if (obj) {
            transformControls.attach(obj);
            // Hide scale controls for Trees (we scale via properties sidebar parameters for stability)
            if (obj.userData.type === 'tree') {
                transformControls.setMode('translate');
            }
        } else {
            transformControls.detach();
        }
        updatePropertiesPanel();
    }

    function deleteSelected() {
        if (!selectedObject) return;
        transformControls.detach();
        scene.remove(selectedObject);
        objects = objects.filter(o => o !== selectedObject);
        selectedObject = null;
        updatePropertiesPanel();
        if (window.markSceneTabDirty) window.markSceneTabDirty();
    }

    function duplicateSelected() {
        if (!selectedObject) return;
        const u = selectedObject.userData;
        const pos = selectedObject.position;
        const scale = selectedObject.scale;
        const rot = selectedObject.rotation;
        
        // Spawn shifted slightly to side
        const offset = snapEnabled ? snapValue : 0.5;

        if (u.type === 'cube') {
            spawnBox(pos.x + offset, pos.y, pos.z, scale.x, scale.y, scale.z, u.color);
        } else if (u.type === 'tree') {
            spawnTree(pos.x + offset, pos.y, pos.z, u.trunkHeight, u.foliageSize);
        } else if (u.type === 'model') {
            spawnModelAsset(u.modelPath, pos.x + offset, pos.y, pos.z, scale.x, scale.y, scale.z, rot.x, rot.y, rot.z, u.color);
        }
    }

    function confirmClearScene() {
        if (confirm('Are you sure you want to clear all objects from the scene?')) {
            clear();
            if (window.markSceneTabDirty) window.markSceneTabDirty();
        }
    }

    function clear() {
        selectObject(null);
        objects.forEach(obj => {
            scene.remove(obj);
        });
        objects = [];
        updatePropertiesPanel();
    }

    // ----------------------------------------------------
    // Property Panel Binding / UI Controls
    // ----------------------------------------------------

    function updatePropertiesPanel() {
        const container = document.getElementById('properties-content');
        if (!container) return;

        if (!selectedObject) {
            container.innerHTML = `
                <div class="empty-selection-msg">
                    <i data-lucide="mouse-pointer"></i>
                    <p>Select an object in the scene to inspect or modify its properties.</p>
                </div>
            `;
            if (window.lucide) window.lucide.createIcons();
            return;
        }

        const u = selectedObject.userData;
        const pos = selectedObject.position;
        const rot = selectedObject.rotation;
        const scale = selectedObject.scale;

        // Transform Gizmo properties
        let propertiesHtml = `
            <div class="inspector-section">
                <div class="inspector-title">Position</div>
                <div class="field-row">
                    <label>Translate</label>
                    <div class="coord-group">
                        <div class="coord-input-wrapper"><span class="coord-label">X</span><input type="number" step="0.1" class="coord-input" id="prop-pos-x" value="${pos.x.toFixed(2)}"></div>
                        <div class="coord-input-wrapper"><span class="coord-label">Y</span><input type="number" step="0.1" class="coord-input" id="prop-pos-y" value="${pos.y.toFixed(2)}"></div>
                        <div class="coord-input-wrapper"><span class="coord-label">Z</span><input type="number" step="0.1" class="coord-input" id="prop-pos-z" value="${pos.z.toFixed(2)}"></div>
                    </div>
                </div>
            </div>
        `;

        if (u.type !== 'tree') {
            // Rotation section (degrees)
            const rxDeg = THREE.MathUtils.radToDeg(rot.x);
            const ryDeg = THREE.MathUtils.radToDeg(rot.y);
            const rzDeg = THREE.MathUtils.radToDeg(rot.z);

            propertiesHtml += `
                <div class="inspector-section">
                    <div class="inspector-title">Rotation</div>
                    <div class="field-row">
                        <label>Angles (°)</label>
                        <div class="coord-group">
                            <div class="coord-input-wrapper"><span class="coord-label">X</span><input type="number" step="5" class="coord-input" id="prop-rot-x" value="${Math.round(rxDeg)}"></div>
                            <div class="coord-input-wrapper"><span class="coord-label">Y</span><input type="number" step="5" class="coord-input" id="prop-rot-y" value="${Math.round(ryDeg)}"></div>
                            <div class="coord-input-wrapper"><span class="coord-label">Z</span><input type="number" step="5" class="coord-input" id="prop-rot-z" value="${Math.round(rzDeg)}"></div>
                        </div>
                    </div>
                </div>
                
                <div class="inspector-section">
                    <div class="inspector-title">Scale / Size</div>
                    <div class="field-row">
                        <label>Dimensions</label>
                        <div class="coord-group">
                            <div class="coord-input-wrapper"><span class="coord-label">X</span><input type="number" step="0.1" min="0.01" class="coord-input" id="prop-scale-x" value="${scale.x.toFixed(2)}"></div>
                            <div class="coord-input-wrapper"><span class="coord-label">Y</span><input type="number" step="0.1" min="0.01" class="coord-input" id="prop-scale-y" value="${scale.y.toFixed(2)}"></div>
                            <div class="coord-input-wrapper"><span class="coord-label">Z</span><input type="number" step="0.1" min="0.01" class="coord-input" id="prop-scale-z" value="${scale.z.toFixed(2)}"></div>
                        </div>
                    </div>
                </div>
            `;
        }

        // Object specific parameters
        if (u.type === 'tree') {
            propertiesHtml += `
                <div class="inspector-section">
                    <div class="inspector-title">Tree Parameters</div>
                    <div class="field-row">
                        <label>Trunk Height</label>
                        <input type="number" step="0.1" min="0.1" class="text-input" id="prop-tree-th" value="${u.trunkHeight.toFixed(2)}">
                    </div>
                    <div class="field-row">
                        <label>Foliage Size</label>
                        <input type="number" step="0.1" min="0.1" class="text-input" id="prop-tree-fs" value="${u.foliageSize.toFixed(2)}">
                    </div>
                </div>
            `;
        } else if (u.type === 'model') {
            propertiesHtml += `
                <div class="inspector-section">
                    <div class="inspector-title">Model Metadata</div>
                    <div class="field-row">
                        <label>Path</label>
                        <input type="text" class="text-input" id="prop-model-path" value="${u.modelPath}" disabled>
                    </div>
                </div>
            `;
        }

        // Color properties for boxes and models
        if (u.type === 'cube' || u.type === 'model') {
            const activeColor = u.color;
            const colors = ['red', 'blue', 'green', 'yellow', 'orange', 'cyan', 'magenta', 'white', 'gray', 'brown'];
            let colorDots = '';
            colors.forEach(col => {
                const isSelected = activeColor.toLowerCase() === col ? 'selected' : '';
                const hexColor = hexFromColor(col);
                colorDots += `<div class="color-dot ${isSelected}" data-color="${col}" style="background-color: ${hexColor}" title="${col}"></div>`;
            });

            propertiesHtml += `
                <div class="inspector-section">
                    <div class="inspector-title">Material Color</div>
                    <div class="field-row" style="align-items: flex-start;">
                        <label>Color Palette</label>
                        <div class="palette-container">
                            ${colorDots}
                        </div>
                    </div>
                    <div class="field-row custom-picker-row">
                        <label>Hex Picker</label>
                        <input type="color" id="prop-color-picker" value="${hexFromColor(activeColor)}">
                        <span class="custom-color-text" id="prop-color-picker-text">${hexFromColor(activeColor)}</span>
                    </div>
                </div>
            `;
        }

        // Actions section (Delete / Duplicate)
        propertiesHtml += `
            <div class="inspector-section" style="border-bottom: none; margin-top: auto;">
                <div class="action-btn-row">
                    <button class="secondary-btn" id="btn-prop-duplicate" title="Duplicate selection"><i data-lucide="copy"></i> Duplicate</button>
                    <button class="danger-btn" id="btn-prop-delete" title="Delete selected object"><i data-lucide="trash-2"></i> Delete</button>
                </div>
            </div>
        `;

        container.innerHTML = propertiesHtml;
        if (window.lucide) window.lucide.createIcons();

        // Bind Sidebar Input Listeners
        // Position X, Y, Z
        document.getElementById('prop-pos-x').onchange = (e) => { pos.x = parseFloat(e.target.value) || 0; selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
        document.getElementById('prop-pos-y').onchange = (e) => { pos.y = parseFloat(e.target.value) || 0; selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
        document.getElementById('prop-pos-z').onchange = (e) => { pos.z = parseFloat(e.target.value) || 0; selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };

        // Rotation & scale if not a tree
        if (u.type !== 'tree') {
            document.getElementById('prop-rot-x').onchange = (e) => { rot.x = THREE.MathUtils.degToRad(parseFloat(e.target.value) || 0); selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
            document.getElementById('prop-rot-y').onchange = (e) => { rot.y = THREE.MathUtils.degToRad(parseFloat(e.target.value) || 0); selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
            document.getElementById('prop-rot-z').onchange = (e) => { rot.z = THREE.MathUtils.degToRad(parseFloat(e.target.value) || 0); selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };

            document.getElementById('prop-scale-x').onchange = (e) => { scale.x = Math.max(0.01, parseFloat(e.target.value) || 1); selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
            document.getElementById('prop-scale-y').onchange = (e) => { scale.y = Math.max(0.01, parseFloat(e.target.value) || 1); selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
            document.getElementById('prop-scale-z').onchange = (e) => { scale.z = Math.max(0.01, parseFloat(e.target.value) || 1); selectObject(selectedObject); if (window.markSceneTabDirty) window.markSceneTabDirty(); };
        }

        // Tree parameters
        if (u.type === 'tree') {
            document.getElementById('prop-tree-th').onchange = (e) => {
                const val = Math.max(0.1, parseFloat(e.target.value) || 1.5);
                u.trunkHeight = val;
                rebuildTree(selectedObject);
                if (window.markSceneTabDirty) window.markSceneTabDirty();
            };
            document.getElementById('prop-tree-fs').onchange = (e) => {
                const val = Math.max(0.1, parseFloat(e.target.value) || 2.0);
                u.foliageSize = val;
                rebuildTree(selectedObject);
                if (window.markSceneTabDirty) window.markSceneTabDirty();
            };
        }

        // Color clicks
        if (u.type === 'cube' || u.type === 'model') {
            container.querySelectorAll('.color-dot').forEach(dot => {
                dot.onclick = () => {
                    const col = dot.dataset.color;
                    u.color = col;
                    applyColor(selectedObject, col);
                    updatePropertiesPanel();
                    if (window.markSceneTabDirty) window.markSceneTabDirty();
                };
            });

            // Hex picker change
            const picker = document.getElementById('prop-color-picker');
            const pickerText = document.getElementById('prop-color-picker-text');
            picker.oninput = (e) => {
                const hexVal = e.target.value;
                pickerText.textContent = hexVal;
                u.color = hexVal;
                applyColor(selectedObject, hexVal);
                if (window.markSceneTabDirty) window.markSceneTabDirty();
            };
        }

        // Property operations
        document.getElementById('btn-prop-duplicate').onclick = duplicateSelected;
        document.getElementById('btn-prop-delete').onclick = deleteSelected;
    }

    // Helper to apply materials color on mesh or model hierarchy
    function applyColor(target, colorVal) {
        const materialColor = new THREE.Color(hexFromColor(colorVal));
        target.traverse(child => {
            if (child.isMesh && child.material) {
                child.material.color = materialColor;
            }
        });
    }

    // Rebuild the Tree group nodes dynamically when dimensions change
    function rebuildTree(treeGroup) {
        const th = treeGroup.userData.trunkHeight;
        const fs = treeGroup.userData.foliageSize;

        // Find trunk cylinder and leaves cone
        const trunkMesh = treeGroup.children[0];
        const leavesMesh = treeGroup.children[1];

        // Recreate trunk mesh geometry
        trunkMesh.geometry.dispose();
        trunkMesh.geometry = new THREE.CylinderGeometry(0.15, 0.15, th, 6);
        trunkMesh.position.y = th / 2;

        // Recreate foliage mesh geometry
        leavesMesh.geometry.dispose();
        leavesMesh.geometry = new THREE.ConeGeometry(fs / 2, fs * 1.5, 6);
        leavesMesh.position.y = th + (fs * 1.5) / 2;
    }

    // ----------------------------------------------------
    // Serialization & Parsing (Load/Save Scene)
    // ----------------------------------------------------

    // Deserialization: Parse line-by-line format (.epscene) and load objects
    function loadScene(content) {
        clear();
        
        if (!content || !content.trim()) {
            // Spawn a default grass floor base plate (like Roblox Studio baseplate)
            spawnBox(0, -0.5, 0, 100, 1, 100, 'forestgreen');
            selectObject(null);
            if (window.markSceneTabDirty) window.markSceneTabDirty();
            return;
        }

        const lines = content.split('\n');
        for (let i = 0; i < lines.length; i++) {
            let line = lines[i].trim();
            if (!line || line.startsWith('#')) continue;

            const parts = line.split(/\s+/);
            const type = parts[0];

            if (type === 'grid') {
                // Read grid helper config (grid x z size spacing color)
                const size = parseFloat(parts[3]) || 30;
                const spacing = parseFloat(parts[4]) || 2;
                const colName = parts[5] || 'gray';
                
                scene.remove(gridHelper);
                gridHelper = new THREE.GridHelper(size, Math.round(size / spacing), new THREE.Color(hexFromColor(colName)), 0x243b55);
                scene.add(gridHelper);
            } else if (type === 'cube' || type === 'box') {
                // Format: cube x y z sx sy sz color
                const px = parseFloat(parts[1]) || 0;
                const py = parseFloat(parts[2]) || 0.5;
                const pz = parseFloat(parts[3]) || 0;
                const sx = parseFloat(parts[4]) || 1;
                const sy = parseFloat(parts[5]) || 1;
                const sz = parseFloat(parts[6]) || 1;
                const color = parts[7] || defaultColor;
                spawnBox(px, py, pz, sx, sy, sz, color);
            } else if (type === 'tree') {
                // Format: tree x y z trunkHeight foliageSize
                const px = parseFloat(parts[1]) || 0;
                const py = parseFloat(parts[2]) || 0;
                const pz = parseFloat(parts[3]) || 0;
                const th = parseFloat(parts[4]) || 1.5;
                const fs = parseFloat(parts[5]) || 2.0;
                spawnTree(px, py, pz, th, fs);
            } else if (type === 'model') {
                // Format: model x y z sx sy sz rx ry rz path color
                const px = parseFloat(parts[1]) || 0;
                const py = parseFloat(parts[2]) || 0;
                const pz = parseFloat(parts[3]) || 0;
                const sx = parseFloat(parts[4]) || 1;
                const sy = parseFloat(parts[5]) || 1;
                const sz = parseFloat(parts[6]) || 1;
                const rx = parseFloat(parts[7]) || 0;
                const ry = parseFloat(parts[8]) || 0;
                const rz = parseFloat(parts[9]) || 0;
                const path = parts[10];
                const color = parts[11] || 'white';
                spawnModelAsset(path, px, py, pz, sx, sy, sz, rx, ry, rz, color);
            }
        }
        selectObject(null);
    }

    // Serialization: Convert elements back into scene instructions list
    function serialize() {
        const lines = [];
        lines.push("# EpilepsyLang Scene File");
        
        // Export default grid helper settings
        lines.push("grid 0 0 30 2 gray");

        objects.forEach(obj => {
            const u = obj.userData;
            const pos = obj.position;
            const scl = obj.scale;
            const rot = obj.rotation;

            if (u.type === 'cube') {
                lines.push(`cube ${pos.x.toFixed(2)} ${pos.y.toFixed(2)} ${pos.z.toFixed(2)} ${scl.x.toFixed(2)} ${scl.y.toFixed(2)} ${scl.z.toFixed(2)} ${u.color}`);
            } else if (u.type === 'tree') {
                lines.push(`tree ${pos.x.toFixed(2)} ${pos.y.toFixed(2)} ${pos.z.toFixed(2)} ${u.trunkHeight.toFixed(2)} ${u.foliageSize.toFixed(2)}`);
            } else if (u.type === 'model') {
                lines.push(`model ${pos.x.toFixed(2)} ${pos.y.toFixed(2)} ${pos.z.toFixed(2)} ${scl.x.toFixed(2)} ${scl.y.toFixed(2)} ${scl.z.toFixed(2)} ${rot.x.toFixed(2)} ${rot.y.toFixed(2)} ${rot.z.toFixed(2)} ${u.modelPath} ${u.color}`);
            }
        });

        return lines.join('\n');
    }

    // Expose scene editor APIs globally
    window.sceneEditor = {
        init: init,
        loadScene: loadScene,
        serialize: serialize,
        resize: resize,
        clear: clear
    };
})();

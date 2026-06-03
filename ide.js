const http = require('http');
const fs = require('fs/promises');
const path = require('path');
const { spawn, exec } = require('child_process');
const { URL } = require('url');

const PORT = 8000;

// Parse command line arguments
let parentPid = null;
let workspaceArg = null;

for (let i = 2; i < process.argv.length; i++) {
    if (process.argv[i] === '--parent-pid') {
        parentPid = parseInt(process.argv[i + 1], 10);
        i++; // skip pid value
    } else {
        workspaceArg = process.argv[i];
    }
}

const WORKSPACE_DIR = workspaceArg ? path.resolve(workspaceArg) : process.cwd();
const STATIC_DIR = path.join(__dirname, 'ide_files');

// Automatically shut down if parent process dies
if (parentPid && !isNaN(parentPid)) {
    setInterval(() => {
        try {
            process.kill(parentPid, 0);
        } catch (e) {
            console.log(`[IDE] Parent process (${parentPid}) has exited. Shutting down server...`);
            process.exit(0);
        }
    }, 1000);
}

let activeProcess = null;
let debugState = { status: 'running', vars: null };

// Safe path check to prevent directory traversal
function isSafePath(targetPath) {
    if (!targetPath) return false;
    const resolvedBase = path.resolve(WORKSPACE_DIR);
    const resolvedTarget = path.resolve(path.join(WORKSPACE_DIR, targetPath));
    return resolvedTarget.startsWith(resolvedBase);
}

// Map extensions to content types for serving static assets
const mimeTypes = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon'
};

// Recursively walk and list workspace items (folders & files)
async function listWorkspaceFiles(dir, items = []) {
    const ignoredDirs = new Set(['.git', '.vscode', 'ide_files', 'node_modules', 'node-local', 'node-v20.13.1-win-x64']);
    const ignoredExts = new Set(['.exe', '.ilk', '.pdb', '.obj', '.cpp', '.hpp', '.md']);
    const ignoredFiles = new Set(['ide.py', 'ide.js', 'vc140.pdb', 'main.obj', 'corepack', 'corepack.cmd', 'install_tools.bat', 'nodevars.bat', 'npm', 'npm.cmd', 'npx', 'npx.cmd', '.gitignore']);

    const files = await fs.readdir(dir, { withFileTypes: true });

    for (const file of files) {
        if (file.isDirectory()) {
            if (ignoredDirs.has(file.name)) continue;
            const fullPath = path.join(dir, file.name);
            const relPath = path.relative(WORKSPACE_DIR, fullPath).replace(/\\/g, '/');
            items.push({
                name: file.name,
                path: relPath,
                isDir: true
            });
            await listWorkspaceFiles(fullPath, items);
        } else {
            if (ignoredFiles.has(file.name)) continue;
            const ext = path.extname(file.name).toLowerCase();
            if (ignoredExts.has(ext)) continue;

            const fullPath = path.join(dir, file.name);
            const relPath = path.relative(WORKSPACE_DIR, fullPath).replace(/\\/g, '/');
            items.push({
                name: file.name,
                path: relPath,
                isDir: false
            });
        }
    }
    return items;
}

// Global text search in all workspace files
async function searchWorkspaceFiles(query, dir = WORKSPACE_DIR, results = {}) {
    const ignoredDirs = new Set(['.git', '.vscode', 'ide_files', 'node_modules', 'node-local', 'node-v20.13.1-win-x64']);
    const ignoredExts = new Set(['.exe', '.ilk', '.pdb', '.obj', '.cpp', '.hpp', '.md']);
    const ignoredFiles = new Set(['ide.py', 'ide.js', 'vc140.pdb', 'main.obj', 'corepack', 'corepack.cmd', 'install_tools.bat', 'nodevars.bat', 'npm', 'npm.cmd', 'npx', 'npx.cmd', '.gitignore']);

    const files = await fs.readdir(dir, { withFileTypes: true });

    for (const file of files) {
        const fullPath = path.join(dir, file.name);
        if (file.isDirectory()) {
            if (ignoredDirs.has(file.name)) continue;
            await searchWorkspaceFiles(query, fullPath, results);
        } else {
            if (ignoredFiles.has(file.name)) continue;
            const ext = path.extname(file.name).toLowerCase();
            if (ignoredExts.has(ext)) continue;

            try {
                const content = await fs.readFile(fullPath, 'utf8');
                const lines = content.split(/\r?\n/);
                const matches = [];

                lines.forEach((line, idx) => {
                    if (line.toLowerCase().includes(query.toLowerCase())) {
                        matches.push({
                            lineNum: idx + 1,
                            lineContent: line.trim()
                        });
                    }
                });

                if (matches.length > 0) {
                    const relPath = path.relative(WORKSPACE_DIR, fullPath).replace(/\\/g, '/');
                    results[relPath] = matches;
                }
            } catch (err) {
                // Ignore read errors
            }
        }
    }
    return results;
}

const server = http.createServer(async (req, res) => {
    // Add CORS headers for testing
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    const parsedUrl = new URL(req.url, `http://localhost:${PORT}`);
    const pathname = parsedUrl.pathname;

    try {
        // --- API ROUTES ---

        // 0. GET: Ping / Health Check
        if (req.method === 'GET' && pathname === '/api/ping') {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ active: true }));
            return;
        }

        // 1. GET: List Files
        if (req.method === 'GET' && pathname === '/api/files') {
            const filesList = await listWorkspaceFiles(WORKSPACE_DIR);
            filesList.sort((a, b) => {
                if (a.isDir && !b.isDir) return -1;
                if (!a.isDir && b.isDir) return 1;
                return a.path.localeCompare(b.path);
            });
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(filesList));
            return;
        }

        // 2. GET: Read File
        else if (req.method === 'GET' && pathname === '/api/read') {
            const filePath = parsedUrl.searchParams.get('path');
            if (!isSafePath(filePath)) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Invalid path' }));
                return;
            }

            const fullPath = path.join(WORKSPACE_DIR, filePath);
            try {
                const content = await fs.readFile(fullPath, 'utf8');
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ content }));
            } catch (err) {
                res.writeHead(404, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'File not found' }));
            }
            return;
        }

        // 3. GET: Global Search
        else if (req.method === 'GET' && pathname === '/api/search') {
            const query = parsedUrl.searchParams.get('query');
            if (!query || !query.trim()) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Query parameter is missing' }));
                return;
            }
            const searchResults = await searchWorkspaceFiles(query.trim());
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(searchResults));
            return;
        }

        // 4. GET: Stream Script Execution (SSE)
        else if (req.method === 'GET' && pathname === '/api/run-stream') {
            const filePath = parsedUrl.searchParams.get('path');
            if (!isSafePath(filePath)) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Invalid path' }));
                return;
            }

            const interpreterExe = path.join(__dirname, 'EpilespyLang.exe');

            res.writeHead(200, {
                'Content-Type': 'text/event-stream',
                'Cache-Control': 'no-cache',
                'Connection': 'keep-alive'
            });

            // Ensure interpreter binary exists
            try {
                await fs.access(interpreterExe);
            } catch (err) {
                const errLog = JSON.stringify({ type: 'stderr', text: '[System Error] EpilespyLang.exe not found! Please build the C++ compiler project first.\n' });
                const closeLog = JSON.stringify({ type: 'close', code: 1 });
                res.write(`data: ${errLog}\n\ndata: ${closeLog}\n\n`);
                res.end();
                return;
            }

            // Kill any active running processes
            if (activeProcess) {
                try { activeProcess.kill(); } catch (e) {}
            }

            debugState = { status: 'running', vars: null };

            activeProcess = spawn(interpreterExe, [filePath], { cwd: WORKSPACE_DIR });

            const sendSSE = (type, text) => {
                res.write(`data: ${JSON.stringify({ type, text })}\n\n`);
            };

            activeProcess.stdout.on('data', (data) => {
                sendSSE('stdout', data.toString());
            });

            activeProcess.stderr.on('data', (data) => {
                sendSSE('stderr', data.toString());
            });

            activeProcess.on('error', (err) => {
                sendSSE('stderr', `[Spawn Error] ${err.message}\n`);
            });

            activeProcess.on('close', (code) => {
                sendSSE('close', code);
                activeProcess = null;
                res.end();
            });

            req.on('close', () => {
                if (activeProcess) {
                    activeProcess.kill();
                    activeProcess = null;
                }
            });
            return;
        }

        // 5. GET: Stream C++ Compiler Build (SSE)
        else if (req.method === 'GET' && pathname === '/api/build') {
            res.writeHead(200, {
                'Content-Type': 'text/event-stream',
                'Cache-Control': 'no-cache',
                'Connection': 'keep-alive'
            });

            const sendSSE = (type, text) => {
                res.write(`data: ${JSON.stringify({ type, text })}\n\n`);
            };

            const compileCmd = 'cl.exe';
            const compileArgs = ['/Zi', '/EHsc', '/std:c++17', '/nologo', '/FeEpilespyLang.exe', 'main.cpp', 'gdi32.lib', 'user32.lib', 'wininet.lib', 'winmm.lib', 'opengl32.lib'];

            const compileProcess = spawn(compileCmd, compileArgs, { cwd: __dirname });

            compileProcess.stdout.on('data', (data) => {
                sendSSE('stdout', data.toString());
            });

            compileProcess.stderr.on('data', (data) => {
                sendSSE('stderr', data.toString());
            });

            compileProcess.on('error', (err) => {
                sendSSE('stderr', (
                    `[Compiler Error] cl.exe failed to start: ${err.message}\n` +
                    `Tip: Ensure you are running this IDE server from a Visual Studio Developer Command Prompt.\n`
                ));
                sendSSE('close', 1);
                res.end();
            });

            compileProcess.on('close', (code) => {
                sendSSE('close', code);
                res.end();
            });
            return;
        }

        // 6. POST: Write File Content
        else if (req.method === 'POST' && pathname === '/api/write') {
            let body = '';
            req.on('data', chunk => body += chunk);
            req.on('end', async () => {
                try {
                    const data = JSON.parse(body);
                    const targetPath = data.path;
                    const content = data.content || '';

                    if (!isSafePath(targetPath)) {
                        res.writeHead(400, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Invalid path' }));
                        return;
                    }

                    const fullPath = path.join(WORKSPACE_DIR, targetPath);
                    await fs.writeFile(fullPath, content, 'utf8');
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true }));
                } catch (err) {
                    res.writeHead(500, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: err.message }));
                }
            });
            return;
        }

        // 7. POST: Create File/Folder
        else if (req.method === 'POST' && pathname === '/api/create') {
            let body = '';
            req.on('data', chunk => body += chunk);
            req.on('end', async () => {
                try {
                    const data = JSON.parse(body);
                    const name = data.name;
                    const isFolder = data.isFolder || false;

                    if (!isSafePath(name)) {
                        res.writeHead(400, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Invalid path name' }));
                        return;
                    }

                    const fullPath = path.join(WORKSPACE_DIR, name);
                    try {
                        await fs.access(fullPath);
                        res.writeHead(409, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Item already exists' }));
                        return;
                    } catch (e) {
                        // File doesn't exist, proceed to create
                    }

                    if (isFolder) {
                        await fs.mkdir(fullPath, { recursive: true });
                    } else {
                        await fs.mkdir(path.dirname(fullPath), { recursive: true });
                        await fs.writeFile(fullPath, '', 'utf8');
                    }

                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true }));
                } catch (err) {
                    res.writeHead(500, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: err.message }));
                }
            });
            return;
        }

        // 8. POST: Delete File/Folder
        else if (req.method === 'POST' && pathname === '/api/delete') {
            let body = '';
            req.on('data', chunk => body += chunk);
            req.on('end', async () => {
                try {
                    const data = JSON.parse(body);
                    const targetPath = data.path;

                    if (!isSafePath(targetPath)) {
                        res.writeHead(400, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Invalid path' }));
                        return;
                    }

                    const fullPath = path.join(WORKSPACE_DIR, targetPath);
                    const stat = await fs.stat(fullPath);

                    if (stat.isDirectory()) {
                        await fs.rm(fullPath, { recursive: true, force: true });
                    } else {
                        await fs.unlink(fullPath);
                    }

                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true }));
                } catch (err) {
                    res.writeHead(500, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: err.message }));
                }
            });
            return;
        }

        // 9. POST: Rename File/Folder
        else if (req.method === 'POST' && pathname === '/api/rename') {
            let body = '';
            req.on('data', chunk => body += chunk);
            req.on('end', async () => {
                try {
                    const data = JSON.parse(body);
                    const oldPath = data.oldPath;
                    const newName = data.newName;

                    if (!isSafePath(oldPath) || !newName) {
                        res.writeHead(400, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Invalid parameters' }));
                        return;
                    }

                    const fullOld = path.join(WORKSPACE_DIR, oldPath);
                    const parentDir = path.dirname(fullOld);
                    const fullNew = path.join(parentDir, newName);

                    const relNew = path.relative(WORKSPACE_DIR, fullNew);
                    if (!isSafePath(relNew)) {
                        res.writeHead(400, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Invalid destination' }));
                        return;
                    }

                    try {
                        await fs.access(fullNew);
                        res.writeHead(409, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ error: 'Destination already exists' }));
                        return;
                    } catch (e) {
                        // Safe to rename
                    }

                    await fs.rename(fullOld, fullNew);
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true }));
                } catch (err) {
                    res.writeHead(500, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: err.message }));
                }
            });
            return;
        }

        // 10. POST: Stop active process
        else if (req.method === 'POST' && pathname === '/api/stop') {
            if (activeProcess) {
                try { activeProcess.kill(); } catch (e) {}
                activeProcess = null;
            }
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: true }));
            return;
        }

        // 11. POST: Debug Pause (set variables and set status = paused)
        else if (req.method === 'POST' && pathname === '/api/debug-pause') {
            let body = '';
            req.on('data', chunk => body += chunk);
            req.on('end', () => {
                try {
                    const data = JSON.parse(body);
                    debugState.status = 'paused';
                    debugState.vars = data;
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true }));
                } catch (err) {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: err.message }));
                }
            });
            return;
        }

        // 12. GET: Debug Status (returns state status string)
        else if (req.method === 'GET' && pathname === '/api/debug-status') {
            const currentStatus = debugState.status;
            if (currentStatus === 'resume') {
                debugState.status = 'running';
            }
            res.writeHead(200, { 'Content-Type': 'text/plain' });
            res.end(currentStatus);
            return;
        }

        // 13. POST: Debug Resume (set status = resume, clear variables)
        else if (req.method === 'POST' && pathname === '/api/debug-resume') {
            debugState.status = 'resume';
            debugState.vars = null;
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ success: true }));
            return;
        }

        // 14. GET: Full Debug State (for client UI polling)
        else if (req.method === 'GET' && pathname === '/api/debug-state') {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(debugState));
            return;
        }

        // --- STATIC FILE SERVING ---
        else {
            let cleanPath = pathname.replace(/^\//, '');
            if (cleanPath === '') {
                cleanPath = 'index.html';
            }

            let staticFile = path.join(STATIC_DIR, cleanPath);
            try {
                // Try finding it in the ide_files static directory first (for style.css, app.js, etc.)
                await fs.access(staticFile);
                const stat = await fs.stat(staticFile);
                if (stat.isDirectory()) {
                    throw new Error('Is directory');
                }
            } catch (err) {
                // Fallback to the main workspace directory
                staticFile = path.join(WORKSPACE_DIR, cleanPath);
            }

            // Path validation check
            if (!staticFile.startsWith(path.resolve(WORKSPACE_DIR))) {
                res.writeHead(403);
                res.end('Access Denied');
                return;
            }

            try {
                const content = await fs.readFile(staticFile);
                const ext = path.extname(staticFile);
                const contentType = mimeTypes[ext] || 'application/octet-stream';
                res.writeHead(200, { 
                    'Content-Type': contentType,
                    'Cache-Control': 'no-store, no-cache, must-revalidate, proxy-revalidate',
                    'Pragma': 'no-cache',
                    'Expires': '0'
                });

                res.end(content);
            } catch (err) {
                res.writeHead(404);
                res.end('Not Found');
            }
        }
    } catch (error) {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Internal Server Error: ' + error.message }));
    }
});

// Automatically create low-poly models in assets/ folder if not present
async function ensureAssetsDirectoryAndFiles() {
    const assetsDir = path.join(WORKSPACE_DIR, 'assets');
    try {
        await fs.mkdir(assetsDir, { recursive: true });
        
        // 1. tree.obj
        const treePath = path.join(assetsDir, 'tree.obj');
        try {
            await fs.access(treePath);
        } catch (e) {
            const treeContent = generateTreeObj();
            await fs.writeFile(treePath, treeContent, 'utf8');
        }

        // 2. rock.obj
        const rockPath = path.join(assetsDir, 'rock.obj');
        try {
            await fs.access(rockPath);
        } catch (e) {
            const rockContent = generateRockObj();
            await fs.writeFile(rockPath, rockContent, 'utf8');
        }

        // 3. crate.obj
        const cratePath = path.join(assetsDir, 'crate.obj');
        try {
            await fs.access(cratePath);
        } catch (e) {
            const crateContent = generateCrateObj();
            await fs.writeFile(cratePath, crateContent, 'utf8');
        }
    } catch (err) {
        console.error('[Assets Startup Loader] Error generating default assets:', err.message);
    }
}

function generateCrateObj() {
    return `# Generated Crate OBJ
v -0.5 -0.5 -0.5
v 0.5 -0.5 -0.5
v 0.5 0.5 -0.5
v -0.5 0.5 -0.5
v -0.5 -0.5 0.5
v 0.5 -0.5 0.5
v 0.5 0.5 0.5
v -0.5 0.5 0.5
f 1 2 3 4
f 5 6 7 8
f 1 5 8 4
f 2 6 7 3
f 3 7 8 4
f 1 2 6 5
`;
}

function generateTreeObj() {
    let vertices = [];
    let faces = [];
    const segments = 6;
    const trunkHeight = 1.5;
    const trunkRadius = 0.15;
    for (let i = 0; i < segments; i++) {
        let angle = (i / segments) * Math.PI * 2;
        vertices.push([Math.cos(angle) * trunkRadius, 0, Math.sin(angle) * trunkRadius]);
    }
    for (let i = 0; i < segments; i++) {
        let angle = (i / segments) * Math.PI * 2;
        vertices.push([Math.cos(angle) * trunkRadius, trunkHeight, Math.sin(angle) * trunkRadius]);
    }
    for (let i = 0; i < segments; i++) {
        let n1 = i + 1;
        let n2 = ((i + 1) % segments) + 1;
        let n3 = n2 + segments;
        let n4 = n1 + segments;
        faces.push([n1, n2, n3, n4]);
    }
    let f1BaseIdx = vertices.length + 1;
    const f1Radius = 0.6;
    const f1BaseY = trunkHeight - 0.2;
    const f1TopY = trunkHeight + 1.2;
    for (let i = 0; i < segments; i++) {
        let angle = (i / segments) * Math.PI * 2;
        vertices.push([Math.cos(angle) * f1Radius, f1BaseY, Math.sin(angle) * f1Radius]);
    }
    vertices.push([0, f1TopY, 0]);
    let f1TopIdx = vertices.length;
    for (let i = 0; i < segments; i++) {
        let b1 = f1BaseIdx + i;
        let b2 = f1BaseIdx + ((i + 1) % segments);
        faces.push([b1, b2, f1TopIdx]);
    }
    let f1Cap = [];
    for (let i = segments - 1; i >= 0; i--) {
        f1Cap.push(f1BaseIdx + i);
    }
    faces.push(f1Cap);
    let f2BaseIdx = vertices.length + 1;
    const f2Radius = 0.45;
    const f2BaseY = trunkHeight + 0.5;
    const f2TopY = trunkHeight + 2.0;
    for (let i = 0; i < segments; i++) {
        let angle = (i / segments) * Math.PI * 2;
        vertices.push([Math.cos(angle) * f2Radius, f2BaseY, Math.sin(angle) * f2Radius]);
    }
    vertices.push([0, f2TopY, 0]);
    let f2TopIdx = vertices.length;
    for (let i = 0; i < segments; i++) {
        let b1 = f2BaseIdx + i;
        let b2 = f2BaseIdx + ((i + 1) % segments);
        faces.push([b1, b2, f2TopIdx]);
    }
    let f2Cap = [];
    for (let i = segments - 1; i >= 0; i--) {
        f2Cap.push(f2BaseIdx + i);
    }
    faces.push(f2Cap);
    let lines = ["# Generated Low-Poly Tree OBJ"];
    for (let v of vertices) {
        lines.push(`v ${v[0].toFixed(4)} ${v[1].toFixed(4)} ${v[2].toFixed(4)}`);
    }
    for (let f of faces) {
        lines.push(`f ${f.join(' ')}`);
    }
    return lines.join('\n');
}

function generateRockObj() {
    let vertices = [
        [0, 0.6, 0],
        [0.4, 0.1, 0.4],
        [-0.3, 0.2, 0.5],
        [-0.4, 0.1, -0.3],
        [0.3, 0.2, -0.4],
        [0, -0.5, 0],
        [0.5, -0.1, 0],
        [0, 0.1, 0.5],
        [-0.5, -0.1, -0.1],
        [0.1, -0.2, -0.5]
    ];
    let faces = [
        [1, 2, 7], [1, 7, 3], [1, 3, 8], [1, 8, 4], [1, 4, 9], [1, 9, 2],
        [6, 7, 2], [6, 3, 7], [6, 8, 3], [6, 4, 8], [6, 9, 4], [6, 2, 9],
        [2, 10, 7], [7, 10, 3], [3, 10, 8], [8, 10, 4], [4, 10, 9], [9, 10, 2]
    ];
    let rng = 0.12345;
    function nextRand() {
        rng = (rng * 9301 + 49297) % 233280;
        return rng / 233280;
    }
    for (let i = 0; i < vertices.length; i++) {
        vertices[i][0] += (nextRand() - 0.5) * 0.15;
        vertices[i][1] += (nextRand() - 0.5) * 0.15;
        vertices[i][2] += (nextRand() - 0.5) * 0.15;
    }
    let lines = ["# Generated Rock OBJ"];
    for (let v of vertices) {
        lines.push(`v ${v[0].toFixed(4)} ${v[1].toFixed(4)} ${v[2].toFixed(4)}`);
    }
    for (let f of faces) {
        lines.push(`f ${f.join(' ')}`);
    }
    return lines.join('\n');
}

server.listen(PORT, async () => {
    await ensureAssetsDirectoryAndFiles();
    console.log('=========================================================');
    console.log(`   E P I L E P S Y L A N G   V S   I D E   (J A V A S C R I P T)`);
    console.log(`   Listening on: http://localhost:${PORT}`);
    console.log('=========================================================');

    // Launch default web browser natively on Windows using user privilege
    setTimeout(() => {
        try {
            exec(`start http://localhost:${PORT}`);
        } catch (e) {
            // Silently ignore browser launch error
        }
    }, 1000);
});

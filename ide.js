const http = require('http');
const fs = require('fs/promises');
const path = require('path');
const { spawn, exec } = require('child_process');
const { URL } = require('url');

const PORT = 8000;
const WORKSPACE_DIR = process.argv[2] ? path.resolve(process.argv[2]) : process.cwd();
const STATIC_DIR = path.join(__dirname, 'ide_files');

let activeProcess = null;

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
    const ignoredDirs = new Set(['.git', '.vscode', 'ide_files', 'node_modules']);
    const ignoredExts = new Set(['.exe', '.ilk', '.pdb', '.obj']);
    const ignoredFiles = new Set(['ide.py', 'ide.js', 'vc140.pdb', 'main.obj']);

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
    const ignoredDirs = new Set(['.git', '.vscode', 'ide_files', 'node_modules']);
    const ignoredExts = new Set(['.exe', '.ilk', '.pdb', '.obj']);
    const ignoredFiles = new Set(['ide.py', 'ide.js', 'vc140.pdb', 'main.obj']);

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
            const compileArgs = ['/Zi', '/EHsc', '/std:c++17', '/nologo', '/FeEpilespyLang.exe', 'main.cpp'];

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
                res.writeHead(200, { 'Content-Type': contentType });
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

server.listen(PORT, () => {
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

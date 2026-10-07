'use strict';

const buildQuery = new URL(document.currentScript.src).search;
const canvas = document.getElementById('canvas');
const loading = document.getElementById('loading');
const statusNode = document.getElementById('status');
const progressNode = document.getElementById('progress');
let lastDependencyCount = 0;
let syncPending = false;
let syncQueued = false;
let gameReady = false;
let analyticsQueue = Promise.resolve();

// Resize only CSS presentation. SDL owns the backing buffer and input mapping.
function fitCanvas() {
    const stage = document.getElementById('stage');
    const scale = Math.min(stage.clientWidth / canvas.width, stage.clientHeight / canvas.height);
    if (scale > 0) {
        canvas.style.width = Math.floor(canvas.width * scale) + 'px';
        canvas.style.height = Math.floor(canvas.height * scale) + 'px';
    }
}
new ResizeObserver(fitCanvas).observe(document.getElementById('stage'));
new MutationObserver(fitCanvas).observe(canvas, { attributes: true, attributeFilter: ['width', 'height'] });

var Module = {
    locateFile: function(path, prefix) { return prefix + path + buildQuery; },
    reportMatchStats: function(phase, matchID, stats) {
        // Serialize start/end requests and retry transient failures once. This
        // queue never blocks the game and sends only its existing match summary.
        const body = new URLSearchParams({ command: 'gamestats', phase, match_id: matchID, stats });
        const keepalive = new TextEncoder().encode(body.toString()).length <= 60000;
        analyticsQueue = analyticsQueue.then(async function() {
            for (let attempt = 0; attempt < 2; ++attempt) {
                const controller = new AbortController();
                const timer = setTimeout(() => controller.abort(), 3000);
                try {
                    const response = await fetch('/metaserver/metaserver.php', {
                        method: 'POST', body, credentials: 'omit', keepalive, signal: controller.signal
                    });
                    if (response.ok && (await response.text()).trim() === 'OK') return;
                    if (response.status >= 400 && response.status < 500) break;
                } catch (error) {
                    // Offline/timeout is non-fatal; the next attempt is bounded.
                } finally {
                    clearTimeout(timer);
                }
            }
            console.error('Could not record match analytics ' + phase + '.');
        }).catch(function() { console.error('Could not queue match analytics.'); });
        return analyticsQueue;
    },
    defaultVideoSize: function() {
        const stage = document.getElementById('stage');
        if (window.matchMedia('(pointer: coarse)').matches && window.innerWidth < 900) {
            return { width: 854, height: 480 };
        }
        if (stage.clientWidth >= 1920 && stage.clientHeight >= 1080) return { width: 1920, height: 1080 };
        if (stage.clientWidth >= 1600 && stage.clientHeight >= 900) return { width: 1600, height: 900 };
        return { width: 1280, height: 720 };
    },
    canvas,
    preRun: [function() {
        FS.mkdirTree('/home/web_user');
        FS.mount(IDBFS, {}, '/home/web_user');
        addRunDependency('dunecity-idbfs');
        FS.syncfs(true, function(error) {
            if (error) console.error('Could not restore browser saves:', error);
            removeRunDependency('dunecity-idbfs');
        });
    }],
    requestPersistentSync: function() {
        if (typeof FS === 'undefined') return;
        if (syncPending) { syncQueued = true; return; }
        syncPending = true;
        syncQueued = false;
        FS.syncfs(false, function(error) {
            syncPending = false;
            if (error) console.error('Could not save browser data:', error);
            // Writes made during the previous snapshot need another serialized sync.
            if (syncQueued) Module.requestPersistentSync();
        });
    },
    markGameReady: function() {
        gameReady = true;
        loading.hidden = true;
        fitCanvas();
        canvas.focus();
    },
    printErr: function(text) {
        if (String(text).includes('emscripten_set_main_loop_timing: Cannot set timing mode')) return;
        console.error(text);
    },
    setStatus: function(text) {
        if (!text) {
            if (!gameReady) statusNode.textContent = 'Loading graphics and sounds into browser memory.';
            return;
        }
        const match = text.match(/\((\d+(?:\.\d+)?)\/(\d+)\)/);
        if (match) {
            progressNode.max = Number(match[2]);
            progressNode.value = Number(match[1]);
        }
        statusNode.textContent = text.replace(/\s*\(\d+(?:\.\d+)?\/\d+\)\s*/, '');
    },
    monitorRunDependencies: function(count) {
        if (count > lastDependencyCount) lastDependencyCount = count;
        progressNode.max = Math.max(1, lastDependencyCount);
        progressNode.value = lastDependencyCount - count;
    },
    onAbort: function(reason) {
        loading.hidden = false;
        statusNode.classList.add('error');
        statusNode.textContent = 'The browser build stopped: ' + reason;
    }
};

window.addEventListener('error', function(event) {
    loading.hidden = false;
    statusNode.classList.add('error');
    statusNode.textContent = event.message || 'The browser build could not start.';
});
document.addEventListener('visibilitychange', function() {
    if (document.hidden) Module.requestPersistentSync();
});
window.addEventListener('pagehide', function() { Module.requestPersistentSync(); });
canvas.addEventListener('contextmenu', function(event) { event.preventDefault(); });
canvas.addEventListener('webglcontextlost', function(event) {
    event.preventDefault();
    loading.hidden = false;
    statusNode.classList.add('error');
    statusNode.textContent = 'Graphics context lost. Reload the game to continue.';
});
document.getElementById('reload').addEventListener('click', function() {
    Module.requestPersistentSync();
    setTimeout(function() { location.reload(); }, 120);
});
document.getElementById('fullscreen').addEventListener('click', function() {
    const stage = document.getElementById('stage');
    if (document.fullscreenElement) {
        document.exitFullscreen();
        return;
    }
    stage.requestFullscreen().then(function() {
        canvas.focus();
        if (screen.orientation && screen.orientation.lock) {
            screen.orientation.lock('landscape').catch(function() {});
        }
    });
});
setInterval(function() { Module.requestPersistentSync(); }, 30000);

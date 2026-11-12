// Wires src/loader.js to the page: builds the demo tasks, drives the overlay's
// data-state, and reveals the content when loading finishes.
import { runLoader } from "./src/loader.js";

const params = new URLSearchParams(location.search);
const latency = clampInt(params.get("latency"), 1200, 0, 10000);
const failing = new Set((params.get("fail") ?? "").split(",").filter(Boolean));

const loader = document.getElementById("loader");
const progress = document.getElementById("progress");
const status = document.getElementById("status");
const retry = document.getElementById("retry");
const content = document.getElementById("content");
const title = document.getElementById("loader-title");

function clampInt(raw, fallback, min, max) {
  const n = Number.parseInt(raw ?? "", 10);
  return Number.isNaN(n) ? fallback : Math.min(max, Math.max(min, n));
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function maybeFail(name) {
  if (failing.has(name)) throw new Error(`${name} failed to load`);
}

// A generated SVG stands in for a large hero image; decode() resolves once the
// browser could paint it without jank.
function heroDataUrl() {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="640" height="200">
    <defs><linearGradient id="g" x1="0" x2="1"><stop offset="0" stop-color="#3a6df0"/><stop offset="1" stop-color="#e2557a"/></linearGradient></defs>
    <rect width="640" height="200" fill="url(#g)"/>
    <path d="M0 140 Q160 80 320 140 T640 140 V200 H0Z" fill="rgba(255,255,255,.25)"/>
    <path d="M0 165 Q160 115 320 165 T640 165 V200 H0Z" fill="rgba(255,255,255,.35)"/></svg>`;
  return `data:image/svg+xml,${encodeURIComponent(svg)}`;
}

function buildTasks() {
  return [
    { label: "Fonts", weight: 1, run: () => document.fonts.ready },
    {
      label: "Content",
      weight: 3,
      run: async () => {
        await sleep(latency);
        maybeFail("content");
        const res = await fetch("data.json", { cache: "no-store" });
        if (!res.ok) throw new Error(`data.json returned ${res.status}`);
        return res.json();
      },
    },
    {
      label: "Hero image",
      weight: 2,
      run: async () => {
        await sleep(Math.round(latency * 0.6));
        maybeFail("hero");
        const img = document.getElementById("hero");
        img.src = heroDataUrl();
        await img.decode();
      },
    },
  ];
}

function setProgress(fraction) {
  const pct = Math.round(fraction * 100);
  progress.style.setProperty("--progress", fraction.toFixed(3));
  progress.setAttribute("aria-valuenow", String(pct));
}

// Resolve when the fade-out ends. transitionend never fires when transitions
// are disabled (reduced motion) or the element is hidden, so a timer backs it up.
function afterFade(el, maxMs) {
  return new Promise((resolve) => {
    const done = () => {
      clearTimeout(timer);
      el.removeEventListener("transitionend", onEnd);
      resolve();
    };
    const onEnd = (e) => { if (e.target === el && e.propertyName === "opacity") done(); };
    const timer = setTimeout(done, maxMs);
    el.addEventListener("transitionend", onEnd);
  });
}

function reveal(data) {
  document.getElementById("content-title").textContent = data.title;
  const list = document.getElementById("content-list");
  list.replaceChildren(...data.items.map((text) => Object.assign(document.createElement("li"), { textContent: text })));
  delete document.body.dataset.loading;
  content.setAttribute("aria-busy", "false");
}

async function start() {
  retry.hidden = true;
  title.textContent = "Getting things ready";
  setProgress(0);
  status.textContent = "Starting…";
  // On retry the overlay is already up, so keep it; on first run let the delay decide.
  if (loader.dataset.state === "error") loader.dataset.state = "visible";

  const { shown, results, failures } = await runLoader(buildTasks(), {
    showDelayMs: 150,
    minVisibleMs: 600,
    taskTimeoutMs: 8000,
    onShow: () => { if (loader.dataset.state === "idle") loader.dataset.state = "visible"; },
    onProgress: ({ fraction, label, ok }) => {
      setProgress(fraction);
      if (label) status.textContent = `${label} ${ok ? "ready" : "failed"} (${Math.round(fraction * 100)}%)`;
    },
  });

  if (failures.length > 0) {
    loader.dataset.state = "error";
    title.textContent = "Something went wrong";
    status.textContent = `Couldn't load: ${failures.map((f) => f.label).join(", ")}.`;
    retry.hidden = false;
    retry.focus();
    return;
  }

  reveal(results.find((r) => r.label === "Content").value);
  if (shown || loader.dataset.state === "visible") {
    loader.dataset.state = "leaving";
    await afterFade(loader, 700);
  }
  loader.dataset.state = "done";
  document.documentElement.dataset.loaded = "true";
}

retry.addEventListener("click", () => {
  failing.clear(); // the demo "fixes" itself so a retry can succeed
  start();
});

start();

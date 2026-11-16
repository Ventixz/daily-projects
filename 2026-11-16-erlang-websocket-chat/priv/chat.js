// Browser side of wschat. One "transport" interface with two implementations:
// WebSocket when it works, long-polling when it doesn't (or when the URL has
// ?transport=poll). The UI code only ever calls send() and gets onEvent().
"use strict";

const $ = (id) => document.getElementById(id);
const members = new Set();

function line(html, cls) {
  const li = document.createElement("li");
  if (cls) li.className = cls;
  li.append(...html);
  $("log").append(li);
  li.scrollIntoView({ block: "end" });
}
const span = (text, cls) => Object.assign(document.createElement("span"), { textContent: text, className: cls || "" });

function renderMembers() {
  $("members").textContent = members.size ? "Here: " + [...members].sort().join(", ") : "";
}

function onEvent(ev) {
  switch (ev.type) {
    case "welcome":
      ev.history.forEach((h) => onEvent({ ...h, replay: true }));
      members.clear(); ev.members.forEach((m) => members.add(m)); renderMembers();
      $("join").hidden = true; $("say").hidden = false; $("text").focus();
      break;
    case "msg": line([span(ev.nick, "nick"), span(": " + ev.text)]); break;
    case "join": if (!ev.replay) members.add(ev.nick); line([span(ev.nick + " joined")], "sys"); break;
    case "leave": if (!ev.replay) members.delete(ev.nick); line([span(ev.nick + " left")], "sys"); break;
    case "nick":
      if (!ev.replay) { members.delete(ev.old); members.add(ev.nick); }
      line([span(ev.old + " is now " + ev.nick)], "sys"); break;
    case "error": line([span("error: " + ev.reason)], "err"); break;
  }
  if (!ev.replay) renderMembers();
}

function setStatus(s) { $("status").textContent = s; }

function webSocketTransport(nick) {
  return new Promise((resolve, reject) => {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(`${proto}//${location.host}/ws?nick=${encodeURIComponent(nick)}`);
    let joined = false, refused = false;
    ws.onmessage = (m) => {
      const ev = JSON.parse(m.data);
      if (!joined && ev.type === "error") refused = true; // e.g. nick_taken: falling back won't help
      onEvent(ev);
      if (!joined && ev.type === "welcome") {
        joined = true;
        resolve({ name: "websocket", send: (t) => ws.send(t) });
      }
    };
    // The handshake never completed, or the server refused the join.
    ws.onclose = (e) => {
      if (joined) setStatus(`disconnected (${e.code})`);
      else reject(Object.assign(new Error("websocket failed"), { refused }));
    };
  });
}

async function pollTransport(nick) {
  const r = await fetch("/poll/join", { method: "POST", body: nick });
  const j = await r.json();
  if (!r.ok) { onEvent({ type: "error", reason: j.error }); throw new Error(j.error); }
  const headers = { "X-Chat-Session": j.sid };
  onEvent({ type: "welcome", ...j });
  (async function loop() {
    for (;;) {
      try {
        const res = await fetch("/poll/recv", { headers });
        if (res.status === 404) { setStatus("session expired"); return; }
        (await res.json()).events.forEach(onEvent);
      } catch { await new Promise((ok) => setTimeout(ok, 2000)); } // network blip: back off, retry
    }
  })();
  addEventListener("pagehide", () => fetch("/poll/leave", { method: "POST", headers, keepalive: true }));
  return {
    name: "long-poll",
    send: async (t) => {
      const res = await fetch("/poll/send", { method: "POST", headers, body: t });
      if (!res.ok && res.status !== 404) onEvent({ type: "error", reason: (await res.json()).error });
    },
  };
}

let transport;
$("join").addEventListener("submit", async (e) => {
  e.preventDefault();
  const nick = $("nick").value.trim();
  const forcePoll = new URLSearchParams(location.search).get("transport") === "poll";
  setStatus("connecting…");
  try {
    transport = forcePoll ? await pollTransport(nick)
                          : await webSocketTransport(nick).catch((err) => {
                              if (err.refused) throw err;
                              return pollTransport(nick);
                            });
    setStatus("connected via " + transport.name);
  } catch { setStatus("not connected"); }
});
$("say").addEventListener("submit", (e) => {
  e.preventDefault();
  const t = $("text").value;
  if (t.trim() && transport) transport.send(t);
  $("text").value = "";
});

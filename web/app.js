(function () {
  "use strict";

  var API = String(window.OSU_REPLAYS_API || "").replace(/\/+$/, "");
  var grid = document.getElementById("grid");
  var statusEl = document.getElementById("status");
  var searchEl = document.getElementById("search");
  var modEl = document.getElementById("mod-filter");
  var replays = [];

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function thumbUrl(id) {
    return API + "/t/" + encodeURIComponent(id) + ".jpg";
  }

  function formatDuration(seconds) {
    var total = Math.max(0, Math.round(Number(seconds) || 0));
    var min = Math.floor(total / 60);
    var sec = total % 60;
    return min + ":" + (sec < 10 ? "0" : "") + sec;
  }

  function formatDate(iso) {
    if (!iso) return "";
    var date = new Date(iso);
    if (isNaN(date.getTime())) return "";
    return date.toLocaleDateString();
  }

  function mapLine(replay) {
    var name = [replay.artist, replay.title].filter(Boolean).join(" - ");
    if (replay.difficulty) name += " [" + replay.difficulty + "]";
    return name || "Unknown map";
  }

  function matchesFilter(replay, query, mod) {
    if (mod && (replay.mods || []).indexOf(mod) === -1) return false;
    if (!query) return true;
    var haystack = [
      replay.player,
      replay.artist,
      replay.title,
      replay.difficulty,
      (replay.mods || []).join(" ")
    ].join(" ").toLowerCase();
    return haystack.indexOf(query) !== -1;
  }

  function makeCard(replay) {
    var card = document.createElement("a");
    card.className = "card";
    card.href = "r.html?id=" + encodeURIComponent(replay.id);

    var poster = el("div", "poster");
    var img = document.createElement("img");
    img.loading = "lazy";
    img.alt = "";
    img.addEventListener("error", function () {
      img.remove();
    });
    img.src = thumbUrl(replay.id);
    poster.appendChild(img);

    var body = el("div", "card-body");
    body.appendChild(el("div", "card-title", replay.player || "Unknown player"));
    body.appendChild(el("div", "card-sub", mapLine(replay)));

    var meta = el("div", "card-meta");
    (replay.mods || []).forEach(function (mod) {
      meta.appendChild(el("span", "badge", mod));
    });
    meta.appendChild(el("span", "muted small", formatDuration(replay.duration)));
    meta.appendChild(el("span", "muted small", formatDate(replay.publishedAt)));
    body.appendChild(meta);

    card.appendChild(poster);
    card.appendChild(body);
    return card;
  }

  function render() {
    var query = searchEl.value.trim().toLowerCase();
    var mod = modEl.value;
    var shown = replays.filter(function (replay) {
      return matchesFilter(replay, query, mod);
    });

    grid.textContent = "";
    if (!shown.length) {
      statusEl.textContent = replays.length ? "No matches." : "No replays yet.";
      return;
    }
    statusEl.textContent = "";
    shown.forEach(function (replay) {
      grid.appendChild(makeCard(replay));
    });
  }

  function fillMods() {
    var seen = {};
    replays.forEach(function (replay) {
      (replay.mods || []).forEach(function (mod) {
        if (!seen[mod]) {
          seen[mod] = true;
          var option = document.createElement("option");
          option.value = mod;
          option.textContent = mod;
          modEl.appendChild(option);
        }
      });
    });
  }

  searchEl.addEventListener("input", render);
  modEl.addEventListener("change", render);

  if (!API || API.indexOf("example.workers.dev") !== -1) {
    statusEl.textContent = "Set the Worker URL in config.js first.";
    return;
  }

  fetch(API + "/api/replays")
    .then(function (response) {
      if (!response.ok) throw new Error("HTTP " + response.status);
      return response.json();
    })
    .then(function (data) {
      replays = Array.isArray(data.replays) ? data.replays : [];
      fillMods();
      render();
    })
    .catch(function (error) {
      statusEl.textContent = "Could not load replays: " + error.message;
    });
})();

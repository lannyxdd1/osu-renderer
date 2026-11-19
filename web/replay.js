(function () {
  "use strict";

  var API = String(window.OSU_REPLAYS_API || "").replace(/\/+$/, "");
  var content = document.getElementById("content");

  var GROUPS = [
    {
      key: "render",
      title: "Render",
      rows: [
        ["fps", "FPS", " fps"],
        ["resolution", "Resolution", ""],
        ["cursorScale", "Cursor scale", ""],
        ["encoder", "Encoder", ""],
        ["crf", "CRF", ""],
        ["motionBlur", "Motion blur", ""],
        ["blurSamples", "Blur samples", ""]
      ]
    },
    {
      key: "hud",
      title: "HUD",
      rows: [
        ["score", "Score", ""],
        ["hp", "HP bar", ""],
        ["combo", "Combo counter", ""],
        ["pp", "PP counter", ""],
        ["error", "Hit error meter", ""],
        ["keys", "Key overlay", ""],
        ["mods", "Mods", ""],
        ["leaderboard", "Scoreboard", ""],
        ["hitCounter", "Hit counter", ""],
        ["strain", "Strain graph", ""]
      ]
    },
    {
      key: "playfield",
      title: "Playfield",
      rows: [
        ["dim", "Background dim", "%"],
        ["storyboard", "Storyboards", ""],
        ["video", "Background video", ""],
        ["parallax", "Parallax", ""],
        ["blur", "Background blur", ""],
        ["bloom", "Bloom", ""],
        ["objects", "Hit objects", ""],
        ["approach", "Approach circles", ""],
        ["numbers", "Combo numbers", ""],
        ["follow", "Follow points", ""],
        ["animations", "Hit animations", ""],
        ["beatScale", "Scale to the beat", ""],
        ["snakeIn", "Sliders snake in", ""],
        ["snakeOut", "Sliders snake out", ""]
      ]
    },
    {
      key: "timeline",
      title: "Timeline",
      rows: [
        ["skipIntro", "Skip intro", ""],
        ["start", "Start time", " s"],
        ["end", "End time (0 = end)", " s"],
        ["audioOffset", "Audio offset", " ms"]
      ]
    },
    {
      key: "audio",
      title: "Audio",
      rows: [
        ["musicVolume", "Music volume", "%"],
        ["hitsoundVolume", "Hitsound volume", "%"]
      ]
    }
  ];

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function mapLine(beatmap) {
    var name = [beatmap.artist, beatmap.title].filter(Boolean).join(" - ");
    if (beatmap.difficulty) name += " [" + beatmap.difficulty + "]";
    return name || "Unknown map";
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

  function addBadge(container, text) {
    container.appendChild(el("span", "badge", text));
  }

  function makeRow(label, value, suffix) {
    var row = el("div", "row");
    row.appendChild(el("span", "label", label));
    var text;
    var state = "";
    if (typeof value === "boolean") {
      text = value ? "on" : "off";
      state = value ? "on" : "off";
    } else if (value === undefined || value === null || value === "") {
      text = "—";
      state = "off";
    } else {
      text = String(value) + (suffix || "");
    }
    row.appendChild(el("span", "value " + state, text));
    return row;
  }

  function buildPanel(title, rows) {
    var panel = el("section", "panel");
    panel.appendChild(el("h2", null, title));
    var list = el("div", "rows");
    rows.forEach(function (row) {
      list.appendChild(row);
    });
    panel.appendChild(list);
    return panel;
  }

  function settingsPanels(settings) {
    var panels = [];
    GROUPS.forEach(function (group) {
      var data = settings && settings[group.key];
      if (!data || typeof data !== "object") return;
      var rows = [];
      group.rows.forEach(function (def) {
        var key = def[0];
        var label = def[1];
        var suffix = def[2];
        if (key === "resolution") {
          if (data.width && data.height) {
            rows.push(makeRow(label, data.width + " x " + data.height));
          }
          return;
        }
        if (!(key in data)) return;
        rows.push(makeRow(label, data[key], suffix));
      });
      if (rows.length) panels.push(buildPanel(group.title, rows));
    });
    return panels;
  }

  function statTile(value, label) {
    var tile = el("div", "stat");
    tile.appendChild(el("span", "stat-value", value));
    tile.appendChild(el("span", "stat-label", label));
    return tile;
  }

  function formatNumber(value) {
    if (typeof value !== "number" || !isFinite(value)) return null;
    return value.toLocaleString("en-US");
  }

  function statsPanel(replay, beatmap, video) {
    var panel = el("section", "panel stats");
    panel.appendChild(el("h2", null, "Result"));

    var grid = el("div", "stat-grid");
    // `pp` is not stored in .osr files; the tile only appears if a future
    // version starts sending it.
    if (typeof replay.pp === "number") grid.appendChild(statTile(replay.pp.toFixed(0) + "pp", "PP"));
    var score = formatNumber(replay.score);
    if (score) grid.appendChild(statTile(score, "Score"));
    if (typeof replay.accuracy === "number") {
      grid.appendChild(statTile(replay.accuracy.toFixed(2) + "%", "Accuracy"));
    }
    if (typeof replay.maxCombo === "number") {
      grid.appendChild(statTile(replay.maxCombo + "x", "Max combo"));
    }
    if (typeof replay.misses === "number") {
      grid.appendChild(statTile(String(replay.misses), "Misses"));
    }
    panel.appendChild(grid);

    var rows = el("div", "rows stats-rows");
    var counts = replay.counts || {};
    if (typeof counts["300"] === "number") rows.appendChild(rowNode("300 / 100 / 50", counts["300"] + " / " + (counts["100"] || 0) + " / " + (counts["50"] || 0)));
    rows.appendChild(rowNode("Mods", (replay.mods || []).join(" ") || "No mods"));
    if (beatmap.mapper) rows.appendChild(rowNode("Mapper", beatmap.mapper));
    var difficulty = [];
    if (beatmap.ar) difficulty.push("AR " + beatmap.ar);
    if (beatmap.od) difficulty.push("OD " + beatmap.od);
    if (beatmap.cs) difficulty.push("CS " + beatmap.cs);
    if (beatmap.hp) difficulty.push("HP " + beatmap.hp);
    if (difficulty.length) rows.appendChild(rowNode("Difficulty", difficulty.join("  ·  ")));
    if (beatmap.bpm) rows.appendChild(rowNode("BPM", String(Math.round(beatmap.bpm * 100) / 100)));
    if (beatmap.objects) rows.appendChild(rowNode("Objects", String(beatmap.objects)));
    if (beatmap.length) rows.appendChild(rowNode("Map length", formatDuration(beatmap.length)));
    if (video.duration) rows.appendChild(rowNode("Video length", formatDuration(video.duration)));
    var date = formatDate(replay.replayDate) || formatDate(replay.publishedAt);
    if (date) rows.appendChild(rowNode("Played", date));
    panel.appendChild(rows);

    return panel;
  }

  function showError(text) {
    content.textContent = "";
    content.appendChild(el("div", "error", text));
  }

  function render(replay) {
    var beatmap = replay.beatmap || {};
    var video = replay.video || {};
    var id = replay.id;

    content.textContent = "";

    var playerEl = document.createElement("video");
    playerEl.className = "player";
    playerEl.controls = true;
    playerEl.preload = "metadata";
    playerEl.playsInline = true;
    playerEl.poster = API + "/t/" + encodeURIComponent(id) + ".jpg";
    var source = document.createElement("source");
    // The query string also busts an older cached redirect for the same path.
    source.src = API + "/v/" + encodeURIComponent(id) + ".mp4?src=2";
    source.type = "video/mp4";
    playerEl.appendChild(source);

    var main = el("div", "replay-main");
    main.appendChild(playerEl);

    var head = el("div", "replay-head");
    head.appendChild(el("h1", null, replay.player || "Unknown player"));
    head.appendChild(el("p", "subtitle muted", mapLine(beatmap)));
    if ((replay.mods || []).length) {
      var mods = el("div", "meta-row");
      (replay.mods || []).forEach(function (mod) {
        addBadge(mods, mod);
      });
      head.appendChild(mods);
    }
    main.appendChild(head);

    var actions = el("div", "actions");
    var copyButton = el("button", "button", "Copy link");
    var downloadButton = el("button", "button", "Download settings");
    var copyStatus = el("span", "muted small", "");
    actions.appendChild(copyButton);
    if (replay.profile) actions.appendChild(downloadButton);
    actions.appendChild(copyStatus);
    main.appendChild(actions);

    copyButton.addEventListener("click", function () {
      var link = API + "/r/" + encodeURIComponent(id);
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(link).then(function () {
          copyStatus.textContent = "Link copied.";
        }, function () {
          copyStatus.textContent = link;
        });
      } else {
        copyStatus.textContent = link;
      }
    });

    downloadButton.addEventListener("click", function () {
      var blob = new Blob([JSON.stringify(replay.profile, null, 2)], {
        type: "application/json"
      });
      var url = URL.createObjectURL(blob);
      var link = document.createElement("a");
      link.href = url;
      link.download = "osu-render-" + id + ".json";
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      URL.revokeObjectURL(url);
    });

    var side = el("aside", "replay-side");
    side.appendChild(statsPanel(replay, beatmap, video));

    var layout = el("div", "replay-layout");
    layout.appendChild(main);
    layout.appendChild(side);
    content.appendChild(layout);

    var panels = settingsPanels(replay.settings || {});
    panels.forEach(function (panel) {
      content.appendChild(panel);
    });

    if (replay.settings && replay.settings.skin) {
      content.appendChild(
        buildPanel("Skin", [rowNode("Skin", replay.settings.skin)])
      );
    }

    var details = document.createElement("details");
    details.className = "raw";
    details.appendChild(el("summary", null, "Raw metadata"));
    var pre = document.createElement("pre");
    pre.textContent = JSON.stringify(replay, null, 2);
    details.appendChild(pre);
    content.appendChild(details);
  }

  function rowNode(label, value) {
    var row = el("div", "row");
    row.appendChild(el("span", "label", label));
    row.appendChild(el("span", "value", String(value)));
    return row;
  }

  if (!API || API.indexOf("example.workers.dev") !== -1) {
    showError("Set the Worker URL in config.js first.");
    return;
  }

  var params = new URLSearchParams(window.location.search);
  var id = params.get("id");
  if (!id) {
    showError("Missing replay id.");
    return;
  }

  fetch(API + "/api/replays/" + encodeURIComponent(id))
    .then(function (response) {
      if (response.status === 404) throw new Error("Replay not found.");
      if (!response.ok) throw new Error("HTTP " + response.status);
      return response.json();
    })
    .then(function (replay) {
      document.title = (replay.player || "Replay") + " - osu! replays";
      render(replay);
    })
    .catch(function (error) {
      showError(error.message);
    });
})();

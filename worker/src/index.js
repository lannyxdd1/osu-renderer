// Minimal Cloudflare Worker for osu! replay hosting.
//
// Media lives in GitHub Releases: the desktop app creates a release per replay
// and uploads three assets into it, the Worker serves pointers to them.
// Metadata and the gallery index live in KV.
//
// Bindings required (see wrangler.toml / README):
//   REPLAYS          KV namespace (metadata and index)
// Secrets required:
//   UPLOAD_TOKEN     shared token used by the desktop app
//   GITHUB_TOKEN     fine-grained PAT, Contents: Read and write
// Vars:
//   PUBLIC_BASE      public URL of this worker, no trailing slash
//   PAGES_BASE       GitHub Pages site URL, no trailing slash
//   GITHUB_REPO      owner/repo that stores the releases
//   MEDIA_MODE       "redirect" (default) or "proxy"
//   GITHUB_API_BASE  optional override, default https://api.github.com
//   GITHUB_UPLOAD_BASE  optional override, default https://uploads.github.com
//   GITHUB_WEB_BASE  optional override, default https://github.com

const ID_RE = /^[A-Za-z0-9]{8,24}$/;
const MAX_UPLOAD = 2 * 1024 * 1024 * 1024;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders() });
    }

    if (path === "/" && request.method === "GET") {
      return Response.redirect(env.PAGES_BASE + "/", 302);
    }

    if (path === "/api/health") {
      return json({ ok: true, repo: env.GITHUB_REPO || null });
    }

    if (path === "/api/replays" && request.method === "GET") {
      return listReplays(env);
    }

    let match = path.match(/^\/api\/replays\/([A-Za-z0-9]+)$/);
    if (match) {
      const id = match[1];
      if (request.method === "GET") return getReplay(env, id);
      if (request.method === "POST") return saveReplay(request, env, id);
      if (request.method === "DELETE") return deleteReplay(request, env, id);
    }

    if (path === "/api/upload" && request.method === "POST") {
      return prepareUpload(request, env);
    }

    match = path.match(/^\/r\/([A-Za-z0-9]+)$/);
    if (match && request.method === "GET") {
      return ogPage(env, match[1]);
    }

    match = path.match(/^\/(v|t|p)\/([A-Za-z0-9]+)\.(mp4|jpg)$/);
    if (match) {
      const kind = match[1];
      const id = match[2];
      const ext = match[3];
      if ((kind === "t") !== (ext === "jpg")) return notFound();
      return serveMedia(request, env, kind, id);
    }

    return notFound();
  },
};

function corsHeaders() {
  return {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET,POST,PUT,DELETE,OPTIONS",
    "Access-Control-Allow-Headers": "Authorization,Content-Type",
    "Access-Control-Max-Age": "86400",
  };
}

function json(data, status = 200, extra = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      ...corsHeaders(),
      ...extra,
    },
  });
}

function notFound(text = "Not found") {
  return new Response(text, {
    status: 404,
    headers: { "Content-Type": "text/plain; charset=utf-8", ...corsHeaders() },
  });
}

function authorized(request, env) {
  const header = request.headers.get("Authorization") || "";
  const token = header.startsWith("Bearer ") ? header.slice(7) : "";
  const expected = env.UPLOAD_TOKEN || "";
  if (!token || !expected || token.length !== expected.length) return false;
  let diff = 0;
  for (let i = 0; i < token.length; i++) {
    diff |= token.charCodeAt(i) ^ expected.charCodeAt(i);
  }
  return diff === 0;
}

async function listReplays(env) {
  const index = (await env.REPLAYS.get("index", "json")) || [];
  return json({ replays: index }, 200, { "Cache-Control": "public, max-age=60" });
}

async function getReplay(env, id) {
  if (!ID_RE.test(id)) return notFound();
  const replay = await env.REPLAYS.get("replay:" + id, "json");
  if (!replay) return notFound("Unknown replay");
  return json(replay, 200, { "Cache-Control": "public, max-age=60" });
}

async function saveReplay(request, env, id) {
  if (!authorized(request, env)) return json({ error: "unauthorized" }, 401);
  if (!ID_RE.test(id)) return json({ error: "bad id" }, 400);

  let meta;
  try {
    meta = await request.json();
  } catch {
    return json({ error: "invalid json" }, 400);
  }
  if (!meta || typeof meta !== "object") return json({ error: "invalid body" }, 400);

  meta.id = id;
  meta.publishedAt = new Date().toISOString();
  meta.media = mediaUrls(env, id);
  const summary = {
    id,
    player: string(meta.player),
    artist: string(meta.beatmap && meta.beatmap.artist),
    title: string(meta.beatmap && meta.beatmap.title),
    difficulty: string(meta.beatmap && meta.beatmap.difficulty),
    mods: Array.isArray(meta.mods) ? meta.mods.map(string).slice(0, 16) : [],
    duration: number(meta.video && meta.video.duration),
    publishedAt: meta.publishedAt,
  };

  await env.REPLAYS.put("replay:" + id, JSON.stringify(meta));
  const index = (await env.REPLAYS.get("index", "json")) || [];
  const next = index.filter((item) => item.id !== id);
  next.unshift(summary);
  next.sort((a, b) => String(b.publishedAt).localeCompare(String(a.publishedAt)));
  await env.REPLAYS.put("index", JSON.stringify(next.slice(0, 1000)));

  return json({ ok: true, url: `${env.PUBLIC_BASE}/r/${id}` });
}

async function deleteReplay(request, env, id) {
  if (!authorized(request, env)) return json({ error: "unauthorized" }, 401);
  if (!ID_RE.test(id)) return json({ error: "bad id" }, 400);

  const warnings = [];
  try {
    await deleteRelease(env, id);
  } catch (error) {
    warnings.push((error && error.message) || String(error));
  }

  await env.REPLAYS.delete("replay:" + id);
  const index = (await env.REPLAYS.get("index", "json")) || [];
  await env.REPLAYS.put("index", JSON.stringify(index.filter((item) => item.id !== id)));
  return json(warnings.length ? { ok: true, warnings } : { ok: true });
}

async function prepareUpload(request, env) {
  if (!authorized(request, env)) return json({ error: "unauthorized" }, 401);

  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: "invalid json" }, 400);
  }

  const id = body && body.id;
  if (!id || !ID_RE.test(id)) return json({ error: "bad id" }, 400);

  const files = (body && body.files) || {};
  const specs = [
    ["video", "video.mp4", "video/mp4"],
    ["thumb", "thumb.jpg", "image/jpeg"],
    ["preview", "preview.mp4", "video/mp4"],
  ];

  let release;
  try {
    release = await ensureRelease(env, id);
  } catch (error) {
    return json({ error: String((error && error.message) || error) }, 502);
  }

  const uploads = {};
  for (const [kind, name, contentType] of specs) {
    const file = files[kind];
    if (!file) continue;
    const size = Number(file.size || 0);
    if (!Number.isFinite(size) || size < 0 || size > MAX_UPLOAD) {
      return json({ error: "bad size for " + kind }, 400);
    }
    uploads[kind] = {
      name,
      contentType,
      url: `${uploadBase(env)}/repos/${env.GITHUB_REPO}/releases/${release.id}/assets?name=${encodeURIComponent(name)}`,
    };
  }

  return json({
    id,
    release: { id: release.id, tag: release.tag },
    uploads,
  });
}

// --- GitHub Releases ---

function apiBase(env) {
  return String(env.GITHUB_API_BASE || "https://api.github.com").replace(/\/+$/, "");
}

function uploadBase(env) {
  const base = env.GITHUB_UPLOAD_BASE;
  if (base) return String(base).replace(/\/+$/, "");
  const api = apiBase(env);
  return api === "https://api.github.com" ? "https://uploads.github.com" : `${api}/uploads`;
}

function webBase(env) {
  return String(env.GITHUB_WEB_BASE || "https://github.com").replace(/\/+$/, "");
}

function releaseTag(id) {
  return "replay-" + id;
}

async function gitHub(env, path, init = {}) {
  const token = env.GITHUB_TOKEN;
  if (!token) throw new Error("GITHUB_TOKEN is not configured");

  const headers = {
    Accept: "application/vnd.github+json",
    Authorization: `Bearer ${token}`,
    "User-Agent": "osu-replays-worker",
    "X-GitHub-Api-Version": "2022-11-28",
    ...(init.body ? { "Content-Type": "application/json" } : {}),
    ...(init.headers || {}),
  };

  const response = await fetch(apiBase(env) + path, {
    ...init,
    headers,
    signal: AbortSignal.timeout(15000),
  });

  if (response.status === 404) return null;
  if (!response.ok) {
    let detail = "";
    try {
      const data = await response.json();
      detail = data && data.message ? `: ${data.message}` : "";
    } catch {
      // ignore
    }
    throw new Error(`GitHub API HTTP ${response.status}${detail}`);
  }
  if (response.status === 204) return {};
  return response.json();
}

async function ensureRelease(env, id) {
  const tag = releaseTag(id);
  const existing = await gitHub(env, `/repos/${env.GITHUB_REPO}/releases/tags/${tag}`);
  if (existing) return { id: existing.id, tag };

  const created = await gitHub(env, `/repos/${env.GITHUB_REPO}/releases`, {
    method: "POST",
    body: JSON.stringify({
      tag_name: tag,
      name: `osu! replay ${id}`,
      body: "Rendered replay published from the osu! renderer app.",
      draft: false,
      prerelease: false,
    }),
  });
  return { id: created.id, tag };
}

async function deleteRelease(env, id) {
  const tag = releaseTag(id);
  const release = await gitHub(env, `/repos/${env.GITHUB_REPO}/releases/tags/${tag}`);
  if (!release) return;
  await gitHub(env, `/repos/${env.GITHUB_REPO}/releases/${release.id}`, { method: "DELETE" });
}

function mediaUrls(env, id) {
  const tag = releaseTag(id);
  const base = `${webBase(env)}/${env.GITHUB_REPO}/releases/download/${tag}`;
  return {
    video: `${base}/video.mp4`,
    thumb: `${base}/thumb.jpg`,
    preview: `${base}/preview.mp4`,
  };
}

function mediaKey(kind) {
  if (kind === "v") return "video";
  if (kind === "t") return "thumb";
  return "preview";
}

function contentTypeFor(kind) {
  return kind === "t" ? "image/jpeg" : "video/mp4";
}

async function serveMedia(request, env, kind, id) {
  if (!ID_RE.test(id)) return notFound();

  const meta = await env.REPLAYS.get("replay:" + id, "json");
  if (!meta) return notFound();
  const media = meta.media || mediaUrls(env, id);
  const target = media[mediaKey(kind)];
  if (!target) return notFound();

  // Thumbnails always go through the Worker: the browser and Discord get a
  // plain image/jpeg response instead of a redirect to an attachment.
  const mode = String(env.MEDIA_MODE || "redirect").toLowerCase();
  if (kind !== "t" && mode !== "proxy") {
    return new Response(null, {
      status: 302,
      headers: {
        Location: target,
        "Cache-Control": "public, max-age=3600",
        ...corsHeaders(),
      },
    });
  }

  const headers = {};
  const range = request.headers.get("Range");
  if (range) headers.Range = range;

  let upstream;
  try {
    upstream = await fetch(target, {
      method: request.method === "HEAD" ? "HEAD" : "GET",
      headers,
      signal: AbortSignal.timeout(120000),
    });
  } catch (error) {
    return new Response("Media error: " + ((error && error.message) || error), {
      status: 502,
      headers: { "Content-Type": "text/plain; charset=utf-8", ...corsHeaders() },
    });
  }

  if (upstream.status === 404) return notFound();
  if (!upstream.ok && upstream.status !== 206) {
    return new Response("Media error: HTTP " + upstream.status, {
      status: 502,
      headers: { "Content-Type": "text/plain; charset=utf-8", ...corsHeaders() },
    });
  }

  const out = new Headers();
  for (const name of [
    "content-length",
    "content-range",
    "accept-ranges",
    "etag",
    "last-modified",
  ]) {
    const value = upstream.headers.get(name);
    if (value) out.set(name, value);
  }
  out.set("Content-Type", contentTypeFor(kind));
  out.set(
    "Cache-Control",
    kind === "t" ? "public, max-age=86400" : "public, max-age=3600"
  );
  out.set("Access-Control-Allow-Origin", "*");

  if (request.method === "HEAD") {
    if (upstream.body) await upstream.body.cancel();
    return new Response(null, { status: upstream.status, headers: out });
  }
  return new Response(upstream.body, { status: upstream.status, headers: out });
}

function string(value) {
  return typeof value === "string" ? value.slice(0, 300) : "";
}

function number(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : 0;
}

function escapeHtml(value) {
  return String(value == null ? "" : value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

async function ogPage(env, id) {
  if (!ID_RE.test(id)) return notFound();
  const meta = await env.REPLAYS.get("replay:" + id, "json");
  if (!meta) return notFound("Unknown replay");

  const beatmap = meta.beatmap || {};
  const player = string(meta.player) || "Unknown player";
  const map = [beatmap.artist, beatmap.title].filter(Boolean).join(" - ");
  const title = `${player} - ${map}${beatmap.difficulty ? " [" + beatmap.difficulty + "]" : ""}`;
  const mods = Array.isArray(meta.mods) && meta.mods.length ? meta.mods.join(" ") : "No mods";
  const accuracy = typeof meta.accuracy === "number" ? meta.accuracy.toFixed(2) + "%" : "";
  const description = [mods, accuracy, meta.score ? String(meta.score) + " pts" : ""]
    .filter(Boolean)
    .join("  ·  ");
  const pageUrl = `${env.PUBLIC_BASE}/r/${id}`;
  // The Worker serves the JPEG itself so the crawler sees image/jpeg directly.
  const thumbUrl = `${env.PUBLIC_BASE}/t/${id}.jpg`;
  const target = `${env.PAGES_BASE}/r.html?id=${encodeURIComponent(id)}`;

  const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)}</title>
<meta property="og:type" content="video.other">
<meta property="og:site_name" content="osu! replays">
<meta property="og:title" content="${escapeHtml(title)}">
<meta property="og:description" content="${escapeHtml(description)}">
<meta property="og:url" content="${escapeHtml(pageUrl)}">
<meta property="og:image" content="${escapeHtml(thumbUrl)}">
<meta property="og:image:width" content="1280">
<meta property="og:image:height" content="720">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="${escapeHtml(title)}">
<meta name="twitter:description" content="${escapeHtml(description)}">
<meta name="twitter:image" content="${escapeHtml(thumbUrl)}">
<meta name="robots" content="noindex">
<meta http-equiv="refresh" content="0; url=${escapeHtml(target)}">
</head>
<body>
<p><a href="${escapeHtml(target)}">Open replay</a></p>
</body>
</html>`;

  return new Response(html, {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Cache-Control": "public, max-age=300",
    },
  });
}

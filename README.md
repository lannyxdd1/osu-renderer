# osu! Replay Renderer

> WIP / личный проект. Интерфейс и возможности ещё активно дорабатываются.

Удобный Qt 6 интерфейс для рендера `.osr`-реплеев через [danser-go](https://github.com/Wieku/danser-go): без ручного редактирования JSON-настроек и длинных команд в терминале. Умеет публиковать готовые рендеры на собственный минималистичный сайт.

## Возможности

- Рендер реплеев в видео через danser-go.
- Выбор реплея из сохранённой папки `.osr`.
- Библиотека скинов с выбором по названию.
- FPS, разрешение, размер курсора, кодек, качество и motion blur.
- Настройки HUD, PP counter, фона, объектов и слайдеров.
- Встроенные и пользовательские пресеты.
- Автоматическое определение папки Songs для osu-winello.
- Публикация на сайт: веб-версия 720p60, превью, метаданные из `.osr`/`.osu`,
  все настройки рендера и полный профиль danser.

## Сборка

```bash
cmake -S . -B build
cmake --build build -j2
./build/osu_render
```

Для веб-версии нужен системный `ffmpeg` (пакет `ffmpeg`). Если его нет,
используется ffmpeg из комплекта danser — тогда звук копируется без перекодирования.

Чтобы приложение появилось в меню Openbox в разделе **Applications → Other**:

```bash
install -Dm755 build/osu_render ~/.local/bin/osu_render
install -Dm644 osu-render.desktop ~/.local/share/applications/osu-render.desktop
sed -i "s|^Exec=osu_render|Exec=$HOME/.local/bin/osu_render|" \
    ~/.local/share/applications/osu-render.desktop
obmenu-generator -d   # обновить кэш меню
```

Путь в `Exec` нужен абсолютный: Openbox при запуске не всегда получает `~/.local/bin` в `PATH`.

## Использование

1. В поле **danser-cli** укажите исполняемый файл `danser-cli` из распакованного danser-go.
2. Выберите `.osr` и файл назначения. Папка Songs для osu-winello (`~/osu-wine/osu!/Songs`), `~/.osu` и Wine-префиксов определяется автоматически; при необходимости укажите её вручную в поле **Songs folder**.
3. На вкладке **Skins** добавьте нужные папки скинов и выберите скин по названию.
4. Укажите FPS, разрешение, размер курсора, кодек и CRF; при необходимости задайте пропуск интро, отрезок времени, аудио-смещение и motion blur. Затем нажмите **Start Render**.
5. На вкладке **Publish** укажите Worker URL, publish token и GitHub token, включите **Publish after successful render** или нажмите **Publish last render** после рендера.

Перед запуском приложение записывает отдельный профиль `settings/osu-render.json` рядом с danser. Основной `settings/default.json` не изменяется. В профиль попадут папка Songs, разрешение, FPS, размер курсора, выбранный скин, кодек, CRF и папка выходного файла. В составе релиза danser должен быть FFmpeg.

При добавлении скина выбирается именно его папка (например, `.../Skins/My Skin`), а не файл `skin.ini`. Список скинов и параметры сохраняются в пользовательской конфигурации приложения.

## Сайт с реплеями

Сайт — статическая галерея на GitHub Pages; видео лежат в GitHub Releases
(по одному релизу на реплей), метаданные — в Cloudflare KV. Доступ — через
Worker. Домен не нужен, всё бесплатно.

```
GitHub Pages  →  web/                    галерея и страница реплея
Worker        →  worker/                 API, OG-теги, ссылки на файлы
GitHub Releases + KV → GitHub/Cloudflare  видео, картинки, метаданные
```

Порядок запуска:

1. Разверните Worker по инструкции в [worker/README.md](worker/README.md).
   Там же — публичный репозиторий, fine-grained token GitHub (`Contents:
   Read and write`) и секреты Cloudflare.
2. В `web/config.js` впишите URL воркера.
3. Запушьте репозиторий на GitHub, включите **Settings → Pages → GitHub Actions**.
   Workflow `.github/workflows/pages.yml` сам выложит содержимое `web/`.
4. На вкладке **Publish** в приложении укажите URL воркера, `UPLOAD_TOKEN` и
   GitHub token.
5. После рендера ссылка вида `https://osu-replays.<имя>.workers.dev/r/<id>`
   копируется в буфер обмена — её можно отправлять в Discord.

Каждая публикация создаёт релиз `replay-<id>` с тремя файлами: `video.mp4`
(720p60), `thumb.jpg` и `preview.mp4` (6 секунд). Мастер-файл остаётся на
компьютере. Видео отдаётся браузеру напрямую с GitHub, поэтому трафик не
тарифицируется; GitHub не CDN, так что в релизах хранится только контент
проекта.

Реплеи публичные: любой, кто знает адрес сайта, видит галерею. Ссылки содержат
случайный id, поэтому без ссылки реплей не найти, но это не защита паролем.

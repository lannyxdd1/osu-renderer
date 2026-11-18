# osu! replays — Worker

Небольшой Cloudflare Worker: принимает публикации от приложения, хранит
метаданные в KV, а видео — в **GitHub Releases**. Отдаёт галерее JSON, Discord —
OG-теги, а браузеру — ссылки на файлы.

Стоимость: 0 ₽. Cloudflare Workers и KV на free-плане карту не требуют, GitHub
Releases бесплатны.

## Что нужно

- Аккаунт Cloudflare (бесплатно, карта не нужна).
- **Публичный** репозиторий GitHub — в нём будут лежать релизы с видео. Pages на
  free-плане тоже работает только из публичного репозитория.
- Fine-grained token GitHub с правом **Contents: Read and write**.

## 1. GitHub token

GitHub → **Settings → Developer settings → Personal access tokens →
Fine-grained tokens → Generate new token**:

- **Repository access**: Only select repositories → ваш репозиторий.
- **Permissions → Repository permissions → Contents: Read and write** — этого
  достаточно и для релизов, и для загрузки файлов.
- **Expiration**: на ваш выбор. Когда токен истечёт, публикация перестанет
  работать — просто выпустите новый и обновите секрет.

Скопируйте токен (`github_pat_…`). Он нужен в двух местах: как секрет Worker и
на вкладке Publish в приложении.

## 2. Cloudflare

Создайте KV namespace:

```bash
npx wrangler kv namespace create REPLAYS
```

Полученный id вставьте в `wrangler.toml` вместо `REPLACE_WITH_KV_NAMESPACE_ID`.
Там же проверьте `GITHUB_REPO` (вида `user/repo`) и `PAGES_BASE`.

Секреты (в git не попадают):

```bash
npx wrangler secret put UPLOAD_TOKEN     # openssl rand -hex 24
npx wrangler secret put GITHUB_TOKEN     # github_pat_…
```

## 3. Деплой

```bash
npx wrangler deploy
curl https://osu-replays.<имя>.workers.dev/api/health
```

Wrangler покажет адрес. Впишите его в `PUBLIC_BASE` и задеплойте ещё раз —
иначе ссылки в ответах будут неполными:

```bash
npx wrangler deploy
```

## 4. Приложение

На вкладке **Publish**: Worker URL, publish token (`UPLOAD_TOKEN`) и GitHub
token. Дальше — «Publish last render» или галочка автоматической публикации.

## Как это работает

1. Приложение просит `POST /api/upload` — Worker создаёт релиз
   `replay-<id>` и возвращает три ссылки на загрузку.
2. Приложение само заливает `video.mp4`, `thumb.jpg`, `preview.mp4` в этот
   релиз напрямую в GitHub, со своим токеном. Через Worker видео не проходит.
3. `POST /api/replays/<id>` сохраняет метаданные и ссылки на файлы в KV.
4. `/v/` и `/p/` отвечают `302` на файл в GitHub — Range-запросы обрабатывает
   GitHub, перемотка работает. `/t/` Worker отдаёт сам: так Discord получает
   честный `image/jpeg`, а не редирект на вложение.
5. `DELETE` удаляет релиз вместе с файлами.

Названия файлов внутри релиза фиксированные, поэтому адреса не меняются:
`https://github.com/<user>/<repo>/releases/download/replay-<id>/video.mp4`.

## Маршруты

| Метод | Путь | Назначение |
|---|---|---|
| GET | `/api/replays` | список для галереи |
| GET | `/api/replays/<id>` | полные метаданные |
| POST | `/api/upload` | создать релиз, выдать ссылки (нужен токен) |
| POST | `/api/replays/<id>` | сохранить реплей (нужен токен) |
| DELETE | `/api/replays/<id>` | удалить реплей и релиз (нужен токен) |
| GET | `/r/<id>` | OG-страница для Discord |
| GET | `/v/<id>.mp4` | видео |
| GET | `/t/<id>.jpg` | превью-картинка |
| GET | `/p/<id>.mp4` | короткий клип |

## Ограничения

- **Один файл — до 2 ГБ.** Ролик 720p60 на пару минут весит 15–20 МБ, так что
  запас огромный.
- **Репозиторий должен быть публичным**, иначе релизы не открыть без токена.
- GitHub не CDN. Для личной галереи и сотен просмотров всё нормально, но при
  внезапно огромной нагрузке GitHub может ограничить отдачу — это в их правилах.
  Храните в релизах только контент проекта.
- Cloudflare Workers: 100 000 запросов в день бесплатно. Каждый просмотр — это
  один запрос за превью и один за видео (видео дальше идёт напрямую с GitHub).
- Cloudflare KV: 1 ГБ, 100 000 чтений в день бесплатно.

## Если GitHub плохо открывается

В некоторых сетях download-ссылки GitHub медленные или недоступны. Тогда
поставьте в `wrangler.toml`:

```toml
MEDIA_MODE = "proxy"
```

Worker будет сам забирать видео из GitHub и отдавать с поддержкой Range. Но это
тратит запросы Worker и на free-плане Cloudflare не предназначено для больших
объёмов видео — включайте только при проблемах с доступом.

## Удаление

```bash
curl -X DELETE \
  -H "Authorization: Bearer $UPLOAD_TOKEN" \
  https://osu-replays.<имя>.workers.dev/api/replays/<id>
```

Если релиз удалить не удалось (например, протух токен), реплей всё равно
исчезнет из галереи, а в ответе будет `warnings`. Релиз можно удалить вручную на
странице Releases.

# RESUME

防犯カメラ設置図エディタ。README が使い方とビルド、この文書は続きをやる人向けのメモ。

## 状態 (2026-09-14)

建物と階、固定プロパティ、Safie プレーヤーの3つを足した。**C++ と wasm は一行も
触っていない** — 3つとも index.html だけで閉じている。だから docs/camplan.js は
そのままで、`sed` で docs/index.html を作り直せば済む（build.sh の最後の1行）。

- **建物/階** — ページが `window.camplanStorage`（list/load/save/remove）を置いた
  ときだけ有効。キーは `"建物名/階名"`、値は従来の保存 JSON そのもの。camplan は
  ストレージの実体を知らない（`onCameraOpen` と同じ、ページ側に口を開ける方式）。
  既定は サンプル株式会社/1F、建物を作ると 1F が一緒にできる。編集が止まって
  1.5 秒で自動保存、保存ボタンは「ダウンロード」に変えた。
- **プロパティを4項目に固定** — 名前 / デバイスID / APIキー / コメント
  （`name` / `device_id` / `api_key` / `comment`）。C++ 側は任意キーのままなので
  外から入れた他のキーは壊れずに残る。UI が4つしか見せないだけ。
- **Safie プレーヤー** — ダブルクリックで device_id と api_key が揃っていれば
  SDK を遅延ロードして再生。`window.onCameraOpen` があればそちらが優先。

rapidsos-proto（社内の Lambda サーバ）の `src/web/camplan_wasm` に submodule で
入っている。将来そちら側で `camplanStorage` を object API の `kv_*` に繋いで、
OIDC ログイン後のページを camplan にする予定。camplan 単体でも動くのはそのため。

## 状態 (2026-09-14 その2)

一時URLの発行・通報ボタン・階ごとの緯度経度を足した。**ここで初めて C++ を
触った**（緯度経度）ので、docs/camplan.js を作り直してある。

- **一時URL / 通報** — ページ側が `window.camplanShare.issue()` を用意した
  ときだけヘッダに出る。発行した URL に camplan が `&building=…&floor=…` を
  足すので、受け取った人は発行時に見ていた階が開く。通報は
  `window.onReport(url, info)`（未定義なら alert）。
  `camplanStorage.readOnly` が真なら共有ボタンを出さず、編集 UI と自動保存も止める。
- **緯度・経度** — `Document::lat` / `lon`（`std::optional<double>`）。
  最初はページ側の予約キーに置いていたが、ダウンロードした JSON に入らないので
  C++ に移した。未設定は NaN で表す（`cp_get_lat` が NaN を返し、
  `cp_set_lat(NaN)` で未設定に戻る）。undo では戻らない。

## 状態 (2026-09-09)

カメラに任意の文字列プロパティ（キー/値）を持てるようにした。番号は今まで通り
別扱い（1〜99、重複拒否、描画に使う）で、props はエディタが中身を見ない自由領域。
JSON はカメラごとに `"props":{...}`、クリック/ダブルクリックで
`{ number, props }` がページに渡る（`camplan:select` / `camplan:camera`、
`window.onCameraSelect` / `window.onCameraOpen`）。パネルの編集 API は
`cp_sel_prop_count/key/value`、`cp_sel_set_prop`、`cp_sel_rename_prop`、
`cp_sel_remove_prop`、読み出しは `cp_camera_props(number)`。
文字列は `stringToNewUTF8` / `UTF8ToString` でやりとりして cp_free で返す。

下絵のデコードを C++ 側（stb_image）に移した。ページは受け取ったファイルを
そのまま `cp_set_background(bytes, len, name)` に渡すだけで、Image/canvas/
getImageData は使わない。JSON の中身は元から圧縮画像（base64）で、読み込み時の
デコードも `Document::fromJson` が自分でやるので、JS 側の再デコード往復
（旧 `cp_set_background_pixels`）は消えた。RGBA が境界を越えないので、
2035x1316 の下絵で 10 MB のコピーが 1 往復ぶん減る。代償は wasm が約 45 KB 増。

初版が動く。カメラ配置（クリック→ドラッグで向き）、移動、向き/距離/画角のハンドル、
番号 1〜99（重複拒否）、壁ポリライン、消去、パン/ズーム、ダーク（監視室風）/ライトの
2テーマ、下絵 PNG/JPEG ドロップ、JSON 保存（下絵を base64 内包）、図面解像度の PNG 出力。
ネイティブテスト（BMP 目視 + JSON ラウンドトリップ）と node スモークテストが通る。

## 決めごと

- **依存は stb_image 一枚だけ**。描画は src/raster.cpp の距離場 AA（線=カプセル、
  扇形=円盤×楔の解析被覆）。ポリゴン塗りは存在しない — 必要になったら座標ではなく
  被覆で書くこと。
- 画像デコードは src/image.cpp だけが stb_image を include する。include は
  ファイル名のみ、場所は `-Ithird_party`。ブラウザに画像を触らせない
  （Image/canvas でデコードしない）ので、絵はネイティブと WASM で一致する。
- 下絵は「圧縮ファイル + デコード済み RGBA」の両方を Document が持つ。保存は
  ファイルの方（＝落ちてきた JPEG/PNG そのもの）を base64 で入れる。再エンコードは
  しない — C++ に PNG エンコーダを足さない方針とセット。
- フォントは焼き込み（tools/make_font.py → src/font_data.h、DejaVu 12/16/24px）。
  ブラウザ canvas で文字を描かないこと。ネイティブとの絵の一致が壊れる。
- 座標は world = 下絵ピクセル。角度はスクリーン系で 0°=右、90°=下（y が下向きなので）。
- PNG エンコードだけページ側（canvas.toBlob）。C++ に PNG エンコーダを足さない。
- UI の器（ツールバー・パネル）は HTML/CSS。キャンバス内に UI を作り込まない。
- props は文字列だけ。型を増やさない（数値が欲しいならページ側で parse する）。
  キーは一意・空不可・順序は入力順、制御文字は setProp が落とす。だから JSON の
  エスケープは `"` と `\` の2つで足り、パーサも今のままで済む。
- UI が見せる props は `PROP_FIELDS` の4つだけ。キーは ASCII（`device_id` など）で
  ラベルだけ日本語 — ページ側が `props.device_id` で読めるようにするため。
  増やすときは index.html の `PROP_FIELDS` に足すだけでよく、C++ は触らない。
- ストレージは camplan の外。`window.camplanStorage` が無ければ建物/階は
  丸ごと出さない。camplan が `kv_set`/`kv_get` のような名前を知ってはいけない
  （知った時点で単体で動かなくなる）。
- Safie SDK は再生するまで読み込まない（`loadSafieSdk()` が script を挿す）。
  映像を使わないページが外部へ通信しないように。
- props の編集 API は行単位（set/rename/remove）で、失敗する編集は履歴を積まない。
  パネルが「全消し→全追加」をしないので、1操作＝1 undo が成り立つ。

## 罠

- emcc は PATH に無い。`/c/prog/emsdk/emsdk/upstream/emscripten/emcc` を直接、
  node は `/c/prog/emsdk/emsdk/node/22.16.0_64bit/bin`。
- SINGLE_FILE=1 なので配布物は docs/camplan.js と docs/index.html の2つだけ。
  index.html は web/ が原本で build.sh がコピーする。docs/ 直編集は消える。
- Pages はリポ設定済み（docs/）。デプロイ = build.sh して push。
- undo/redo は選択を外す（App::undo の selected_ = -1）。パネルのプロパティ行は
  選択が無い間 DOM に残るだけで、選択し直せば C++ 側から作り直される。
- tests/page_check.js は docs/ を配信して headless Chrome を CDP で叩く。
  つまり **build.sh を先に走らせないと古い docs/ をテストする**。
  tests/nav_check.py と tests/player_check.py（Playwright）も同じく docs/ を見る。
- `M.lengthBytesUTF8` は **EXPORTED_RUNTIME_METHODS に入っていない**。文字列を
  wasm に渡すときは TextEncoder + `_cp_alloc` + `HEAPU8.set`、読むときは
  TextDecoder（`loadJsonFile` / `currentJson` がその形）。うっかり呼ぶと
  例外が出て、しかも Promise の中だと黙って機能ごと消える。
- `cp_dirty()` は **描画** の dirty であって「文書が変わった」ではない。
  絵が変わらない編集（コメント欄など）は自動保存に拾われないので、
  そういう場所では `touch()` を明示的に呼ぶ。
- 階を消すと `curFlr` が null になる。`touch`/`autoSave`/`flush` の入口で
  `curBld` と `curFlr` の**両方**を見ること（片方だけだと `建物名/null` を書く）。
- MODULARIZE ビルドなのでモジュール実体はクロージャの中。テストから
  `Module` は見えない — 状態を覗きたいときは camplanStorage のモックに
  書かせて、そこを読む。

## 次の候補

- rapidsos-proto 側で `camplanStorage` を object API の `kv_*` に繋ぐ
  （docs/ を server_sample/*/html/camplan/ にコピーする配線と、OIDC の
  リダイレクト先を test_object_api.html から camplan に変える）
- Safie の実機での確認（今はモックでの検証だけ。SDK のバージョン固定も未検討）
- 縮尺（2点クリック+実距離入力で m 表示、距離パネルを m に）
- カメラ一覧の印刷レイアウト（番号・位置・向き・画角の表）
- 壁スナップ（45°/グリッド）、Undo
- ネイティブ/WASM ピクセル一致テスト（flyingtoasters 方式）

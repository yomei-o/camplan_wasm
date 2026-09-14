# camplan - 防犯カメラ設置図エディタ

図面の上に防犯カメラを置き、番号・向き・視野をつけて設置図を作るエディタ。
C++ をスクラッチで書いて WebAssembly にしたもので、描画は全部自前のソフトウェア
ラスタライザ（線・円・扇形・焼き込みフォント）。下絵の JPEG/PNG デコードだけ
stb_image（public domain のヘッダ1枚）で、他に外部ライブラリは無い。

**Play:** https://yomei-o.github.io/camplan_wasm/

## 使い方

| | |
|---|---|
| 下絵 | 図面の PNG/JPEG をドロップ（無ければ方眼紙） |
| カメラ追加 | モードを選んでクリック → そのままドラッグで向きと距離が決まる |
| 編集 | 選択モードでカメラをドラッグ＝移動、先端の丸＝向き/距離、両脇の丸＝画角 |
| 壁 | クリックで頂点、Enter か右クリックで確定 |
| 番号 | 1〜99。右パネルで変更（重複は拒否） |
| プロパティ | 右パネル PROPERTIES で 名前 / デバイスID / APIキー / コメント の4項目。番号と一緒に保存され、クリックでページに渡る |
| マーカー | 右パネル DISPLAY で 〇＋数字 の大きさを調整（保存ファイルに記録） |
| 映像 | カメラをダブルクリック → デバイスIDと APIキーがあれば Safie の映像。ページ側でフックを差せばそちらが優先（下記） |
| 建物/階 | ページ側が `window.camplanStorage` を用意したときだけ右パネルの一番上に出る。建物を選ぶと階が並び、階を選ぶとその図面が開く |
| 保存 | 建物/階が有効なら編集が止まった 1.5 秒後に自動保存。そうでなければヘッダの「ダウンロード」で JSON を落とす |
| JSON | 一つのファイルに下絵画像も base64 で内包 — それだけで復元できる |
| PNG出力 | 図面解像度で書き出し（ハンドル等の UI は入らない） |
| テーマ | ダーク（監視室風）/ ライト（白地に水色方眼） |

## カメラのプロパティ

番号のほかに、カメラ1台ごとに4つの文字列を持たせられる。右パネルの PROPERTIES に
そのまま並んでいて、入力欄を離れた時点で確定する。

| 表示 | JSON のキー | |
|---|---|---|
| 名前 | `name` | 「正面エントランス」など。プレーヤーのタイトルに出る |
| デバイスID | `device_id` | Safie のデバイスID。APIキーと揃うと映像が見られる |
| APIキー | `api_key` | Safie の APIキー |
| コメント | `comment` | 自由記入 |

保存ファイルではカメラごとに `props` として入る:

```json
{"no":1,"x":812,"y":430,"dir":27,"fov":90,"range":171,
 "props":{"name":"正面エントランス","device_id":"dev-xxxx",
          "api_key":"key-xxxx","comment":"受付の上"}}
```

C++ 側は今でも任意のキーを持てる（`cp_sel_set_prop` は何でも受ける）ので、
外から入れた他のキーは保存ファイルに残る。UI がこの4つしか見せないだけ。

## 映像（Safie）

カメラを**ダブルクリック**したとき、`device_id` と `api_key` の両方が入っていれば
Safie の JS SDK をその場で読み込んで再生する。SDK は再生するまで取りに行かない
ので、映像を使わない限り外部への通信は起きない。

```js
Safie.Auth.setToken(props.api_key, 'apiKey');
player = new Safie.Player.StreamingPlayer(box);
player.deviceId = props.device_id;
player.play();               // 閉じると player.stop()
```

どちらかが空なら「設定してください」の案内が出るだけ。Esc か枠の外のクリック、
「閉じる」で止まる。`window.onCameraOpen` を定義すればそちらが優先されるので、
別のプレーヤーに差し替えるのも従来どおりできる。

## ページ側との連携（クリック／ダブルクリック）

カメラを**クリック**して選択すると `{ number, props }` がページに渡り、
**ダブルクリック**すると同じものが「映像を開け」の合図として渡る。
番号は 1〜99、`props` は上の4項目が入ったオブジェクト（無ければ `{}`）。
既定のダブルクリックは上の Safie プレーヤーだが、別のものに差し替えるのも
index.html を編集せずにできる:

```js
// 方法1: フックを置き換える
window.onCameraOpen = function (number, props) {
  // props.device_id / props.api_key / number から好きなプレーヤーを開く
  player.open(props.device_id);
};
window.onCameraSelect = function (number, props) {
  // number === 0 は「選択が外れた」
};

// 方法2: イベントで受ける
window.addEventListener('camplan:camera', function (e) {
  console.log('open', e.detail.number, e.detail.props);
});
window.addEventListener('camplan:select', function (e) {
  console.log('select', e.detail.number, e.detail.props);
});
```

`onCameraOpen` が定義されていれば内蔵プレーヤーは動かない。C++ 側の実体は
`cp_camera_at(x, y)`（画面座標 → カメラ番号、無ければ 0）と
`cp_camera_props(number)`（そのカメラの props を JSON 文字列で）だけで、
どう表示するかはすべてページの自由。

## 建物と階（ページ側のストレージ）

複数の建物・複数の階を扱うのも同じ考え方で、**ページが `window.camplanStorage` を
用意したときだけ**右パネルの一番上に BUILDING / FLOOR のパネルが出る。用意しなければ
今までどおりの単体エディタのまま（ダウンロードと「ファイルを開く」だけ）。

キーは `"建物名/階名"`、値は今まで保存していた JSON 文字列そのもの。camplan は
キーの分解と組み立てしかしないので、実体が何であってもよい（object API の
key-value ストア、localStorage、IndexedDB など）。

```js
window.camplanStorage = {
  // [{ key: "本社ビル/1F", size: 12345 }, ...]  size は任意
  list:   function ()            { return Promise.resolve(rows); },
  load:   function (key)         { return Promise.resolve(jsonText); },
  save:   function (key, json)   { return Promise.resolve(); },
  remove: function (key)         { return Promise.resolve(); },
};
```

4つ全部が関数で、`list()` が成功したときだけパネルが出る。失敗したら黙って
引っ込んで単体動作に戻る（読み込み時に一度だけ試す）。

- 何も無ければ **サンプル株式会社 / 1F** を作る。建物を足すと 1F が一緒にできる
- 最後の建物、その建物の最後の階は消せない。建物を消すと配下の階も全部消える
- 名前に `/` は使えない（キーの区切りなので）。空白だけの名前も拒否
- **自動保存**: 編集が止まって 1.5 秒で `save()`。中身が前と同じなら書かない。
  階や建物を切り替える前には必ず書き切る
- ヘッダの保存ボタンは「ダウンロード」。`建物名_階名.json` で落ちる

## ショートカット

| キー | |
|---|---|
| V / 1 | 選択 |
| C / 2 | カメラ追加（置いたら自動で選択に戻る） |
| W / 3 | 壁 |
| E / 4 | 消去 |
| F | 全体表示 |
| Ctrl+Z / Ctrl+Y | 元に戻す / やり直し（カメラ・壁・マーカーサイズ、100段） |
| Ctrl+S | ダウンロード |
| Del | 選択カメラを削除（壁描画中は直前の頂点を取り消し） |
| Enter / Esc | 壁を確定 / 取り消し |

## ビルド

```sh
# WebAssembly (要 emsdk)
sh tools/build.sh            # -> docs/camplan.js + docs/index.html

# ネイティブテスト（描画を BMP に書いて目視 + JSON ラウンドトリップ）
g++ -O2 -std=c++20 -Ithird_party -o tests/native_test.exe \
    tests/native_test.cpp src/app.cpp src/raster.cpp src/doc.cpp src/image.cpp
(cd tests && ./native_test.exe)

# WASM スモークテスト（node で合成マウス操作）
node tests/node_check.js

# ページのテスト（headless Chrome を CDP で叩く。Chrome が無ければ skip）
node tests/page_check.js            # --shot out.png でスクリーンショット

# 建物/階と Safie プレーヤー（要 Playwright。どちらもモックを差して確かめる）
(cd docs && python -m http.server 8123 &)
python tests/nav_check.py    http://127.0.0.1:8123/
python tests/player_check.py http://127.0.0.1:8123/
```

フォントは `tools/make_font.py` が DejaVu Sans（Bitstream Vera ライセンス）を
一度だけラスタライズして `src/font_data.h` に焼き込む。実行時のフォント処理は
アルファマスクのブリットだけで、ネイティブと WASM の絵が一致する。

## 構成

| | |
|---|---|
| `src/raster.{h,cpp}` | 描画ライブラリ。距離場ベースの AA 線・円・扇形・文字・画像ブリット |
| `src/doc.{h,cpp}` | ドキュメント（カメラ・壁・下絵）と JSON/base64 |
| `src/image.{h,cpp}` | 下絵のデコード（stb_image を include する唯一の場所） |
| `third_party/stb_image.h` | stb_image 2.30（public domain） |
| `src/app.{h,cpp}` | エディタ本体。ツール・ヒットテスト・ハンドル・テーマ・描画 |
| `src/wasm_main.cpp` | WASM 境界（cp_* エクスポート） |
| `web/index.html` | UI シェル（ツールバー・パネル・入出力はページ側） |
| `tests/` | ネイティブ描画テスト、node スモークテスト、headless Chrome のページテスト、建物/階とプレーヤーの Playwright テスト |

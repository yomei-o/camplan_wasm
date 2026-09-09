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
| プロパティ | 右パネル PROPERTIES で任意のキーと値（どちらも文字列）を何個でも。番号と一緒に保存され、クリックでページに渡る |
| マーカー | 右パネル DISPLAY で 〇＋数字 の大きさを調整（保存ファイルに記録） |
| 映像 | カメラをダブルクリック → ページにイベント（下記） |
| 保存 | JSON 一つに下絵画像も base64 で内包 — そのファイルだけで復元できる |
| PNG出力 | 図面解像度で書き出し（ハンドル等の UI は入らない） |
| テーマ | ダーク（監視室風）/ ライト（白地に水色方眼） |

## カメラのプロパティ

番号のほかに、カメラ1台ごとに任意のキーと値（両方とも文字列）を持たせられる。
右パネルの PROPERTIES で「+ 行を追加」→ キーと値を入力。キーは空にできず、
同じカメラ内で重複もできない（拒否してその場で戻る）。行の並びは入力順のまま。
制御文字は入力時に落ちる。保存ファイルではカメラごとに `props` として入る:

```json
{"no":1,"x":812,"y":430,"dir":27,"fov":90,"range":171,
 "props":{"url":"rtsp://cam1/main","室名":"エントランス"}}
```

エディタ自身はこの中身を一切見ない。使うのはページ側:

## ページ側との連携（クリック／ダブルクリック）

カメラを**クリック**して選択すると `{ number, props }` がページに渡り、
**ダブルクリック**すると同じものが「映像を開け」の合図として渡る。
番号は 1〜99、`props` はキーと値がそのまま入ったオブジェクト（無ければ `{}`）。
既定ではダブルクリックで alert のプレースホルダが出るだけなので、実際の
プレーヤーは index.html を編集せずに外から差し込める:

```js
// 方法1: フックを置き換える
window.onCameraOpen = function (number, props) {
  // props.url なり number なりからプレーヤーを開く
  player.open(props.url);
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

`onCameraOpen` が定義されていれば alert は出ない。C++ 側の実体は
`cp_camera_at(x, y)`（画面座標 → カメラ番号、無ければ 0）と
`cp_camera_props(number)`（そのカメラの props を JSON 文字列で）だけで、
どう表示するかはすべてページの自由。

## ショートカット

| キー | |
|---|---|
| V / 1 | 選択 |
| C / 2 | カメラ追加（置いたら自動で選択に戻る） |
| W / 3 | 壁 |
| E / 4 | 消去 |
| F | 全体表示 |
| Ctrl+Z / Ctrl+Y | 元に戻す / やり直し（カメラ・壁・マーカーサイズ、100段） |
| Ctrl+S | 保存 |
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
| `tests/` | ネイティブ描画テスト、node スモークテスト、headless Chrome のページテスト |

# camplan - 防犯カメラ設置図エディタ

図面の上に防犯カメラを置き、番号・向き・視野をつけて設置図を作るエディタ。
C++ をスクラッチで書いて WebAssembly にしたもので、描画は全部自前のソフトウェア
ラスタライザ（線・円・扇形・焼き込みフォント）。外部ライブラリはゼロ。

**Play:** https://yomei-o.github.io/camplan_wasm/

## 使い方

| | |
|---|---|
| 下絵 | 図面の PNG/JPEG をドロップ（無ければ方眼紙） |
| カメラ追加 | モードを選んでクリック → そのままドラッグで向きと距離が決まる |
| 編集 | 選択モードでカメラをドラッグ＝移動、先端の丸＝向き/距離、両脇の丸＝画角 |
| 壁 | クリックで頂点、Enter か右クリックで確定 |
| 番号 | 1〜99。右パネルで変更（重複は拒否） |
| マーカー | 右パネル DISPLAY で 〇＋数字 の大きさを調整（保存ファイルに記録） |
| 映像 | カメラをダブルクリック → ページにイベント（下記） |
| 保存 | JSON 一つに下絵画像も base64 で内包 — そのファイルだけで復元できる |
| PNG出力 | 図面解像度で書き出し（ハンドル等の UI は入らない） |
| テーマ | ダーク（監視室風）/ ライト（白地に水色方眼） |

## 映像プレーヤーの接続（ダブルクリック連携）

カメラの丸をダブルクリックすると、そのカメラの番号がページ側に渡る。
既定では alert のプレースホルダが出るだけ。実際のプレーヤーは index.html を
編集せずに外から差し込める:

```js
// 方法1: フックを置き換える
window.onCameraOpen = function (number) {
  // number (1-99) からストリーム URL を引いてプレーヤーを開く
};

// 方法2: イベントで受ける
window.addEventListener('camplan:camera', function (e) {
  console.log('camera', e.detail.number);
});
```

`onCameraOpen` が定義されていれば alert は出ない。C++ 側の実体は
`cp_camera_at(x, y)`（画面座標 → カメラ番号、無ければ 0）だけで、
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
g++ -O2 -std=c++20 -o tests/native_test.exe \
    tests/native_test.cpp src/app.cpp src/raster.cpp src/doc.cpp
(cd tests && ./native_test.exe)

# WASM スモークテスト（node で合成マウス操作）
node tests/node_check.js
```

フォントは `tools/make_font.py` が DejaVu Sans（Bitstream Vera ライセンス）を
一度だけラスタライズして `src/font_data.h` に焼き込む。実行時のフォント処理は
アルファマスクのブリットだけで、ネイティブと WASM の絵が一致する。

## 構成

| | |
|---|---|
| `src/raster.{h,cpp}` | 描画ライブラリ。距離場ベースの AA 線・円・扇形・文字・画像ブリット |
| `src/doc.{h,cpp}` | ドキュメント（カメラ・壁・下絵）と JSON/base64 |
| `src/app.{h,cpp}` | エディタ本体。ツール・ヒットテスト・ハンドル・テーマ・描画 |
| `src/wasm_main.cpp` | WASM 境界（cp_* エクスポート） |
| `web/index.html` | UI シェル（ツールバー・パネル・入出力はページ側） |
| `tests/` | ネイティブ描画テストと node スモークテスト |

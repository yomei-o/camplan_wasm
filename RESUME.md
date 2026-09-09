# RESUME

防犯カメラ設置図エディタ。README が使い方とビルド、この文書は続きをやる人向けのメモ。

## 状態 (2026-09-09)

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

## 罠

- emcc は PATH に無い。`/c/prog/emsdk/emsdk/upstream/emscripten/emcc` を直接、
  node は `/c/prog/emsdk/emsdk/node/22.16.0_64bit/bin`。
- SINGLE_FILE=1 なので配布物は docs/camplan.js と docs/index.html の2つだけ。
  index.html は web/ が原本で build.sh がコピーする。docs/ 直編集は消える。
- Pages はリポ設定済み（docs/）。デプロイ = build.sh して push。

## 次の候補

- 縮尺（2点クリック+実距離入力で m 表示、距離パネルを m に）
- カメラ一覧の印刷レイアウト（番号・位置・向き・画角の表）
- 壁スナップ（45°/グリッド）、Undo
- ネイティブ/WASM ピクセル一致テスト（flyingtoasters 方式）

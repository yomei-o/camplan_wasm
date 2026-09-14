"""建物/階の機能を、モックの camplanStorage を差してヘッドレスで検証する。

page_check.js と違ってこちらは Playwright（python -m pip install playwright,
python -m playwright install chromium）。camplanStorage は camplan 本体には
無いページ側の口なので、テストもページ側から差し込む形になる。

  (cd docs && python -m http.server 8123 &)
  python tests/nav_check.py http://127.0.0.1:8123/
"""
import os
import sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8123/"

# ページに差し込むモックストレージ（メモリ上の Map）
MOCK = """
window.__kv = new Map();
window.__log = [];
window.camplanStorage = {
  list:   ()          => { window.__log.push('list');
                           return Promise.resolve(
                             [...window.__kv].map(([k, v]) => ({key: k, size: v.length}))); },
  load:   (key)       => { window.__log.push('load:' + key);
                           return Promise.resolve(window.__kv.get(key)); },
  save:   (key, json) => { window.__log.push('save:' + key);
                           window.__kv.set(key, json); return Promise.resolve(); },
  remove: (key)       => { window.__log.push('remove:' + key);
                           window.__kv.delete(key); return Promise.resolve(); },
};
"""

fails = []


def ck(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def keys(pg):
    return sorted(pg.evaluate("[...window.__kv.keys()]"))


def floors(pg):
    return pg.eval_on_selector_all("#flrList .row", "e=>e.map(x=>x.innerText)")


def buildings(pg):
    return pg.eval_on_selector_all("#bldSel option", "e=>e.map(x=>x.value)")


def current_floor(pg):
    return pg.eval_on_selector_all("#flrList .row.on", "e=>e.map(x=>x.innerText)")


def msg(pg):
    return pg.inner_text("#navMsg")


with sync_playwright() as p:
    b = p.chromium.launch(headless=True)

    # ---------------------------------------------------- 単体動作（KVなし）
    print("== A. camplanStorage が無いとき（単体動作）==")
    ctx = b.new_context(viewport={"width": 1280, "height": 860})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(2500)
    ck(pg.eval_on_selector("#navBox", "e=>getComputedStyle(e).display") == "none",
       "建物/階のパネルが出ない")
    # 緯度経度は書類の一部なので、ストレージが無くても使える
    ck(pg.eval_on_selector("#geoBox", "e=>getComputedStyle(e).display") != "none",
       "位置の欄は単体動作でも出る")
    ck(pg.locator("#save").inner_text() == "ダウンロード", "保存ボタンがダウンロードになっている")
    ck(pg.evaluate("!!document.querySelector('#openFile')"), "ファイルを開くは残っている")
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    # ---------------------------------------------------- KV あり
    print("== B. camplanStorage があるとき ==")
    ctx = b.new_context(viewport={"width": 1280, "height": 860})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(MOCK)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3000)

    print("-- 既定の建物/階 --")
    ck(pg.eval_on_selector("#navBox", "e=>getComputedStyle(e).display") != "none",
       "建物/階のパネルが出る")
    ck(keys(pg) == ["サンプル株式会社/1F"], "既定で サンプル株式会社/1F が作られる: %s" % keys(pg))
    ck(buildings(pg) == ["サンプル株式会社"], "建物リスト: %s" % buildings(pg))
    ck(floors(pg) == ["1F"], "階リスト: %s" % floors(pg))
    ck(current_floor(pg) == ["1F"], "1F が選択状態")
    # 白紙だとカメラを置く場所が無いので、既定の階には見本の図面を入れる
    saved = pg.evaluate("window.__kv.get('サンプル株式会社/1F')")
    ck('"background"' in saved or '"bg"' in saved,
       "既定の階に背景図面が入っている（%d バイト）" % len(saved))
    ck(len(saved) > 100000, "背景の実体が入っている: %d バイト" % len(saved))

    print("-- 階を追加 --")
    pg.evaluate("window.prompt = () => '2F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(1200)
    ck(sorted(floors(pg)) == ["1F", "2F"], "階リストに 2F が増える: %s" % floors(pg))
    ck("サンプル株式会社/2F" in keys(pg), "KV に 2F のキーができる")
    ck(current_floor(pg) == ["2F"], "追加した階が選択される")

    print("-- 建物を追加（1F が自動でできる）--")
    pg.evaluate("window.prompt = () => '本社ビル'")
    pg.click("#bldAdd")
    pg.wait_for_timeout(1200)
    ck(sorted(buildings(pg)) == ["サンプル株式会社", "本社ビル"], "建物が増える: %s" % buildings(pg))
    ck("本社ビル/1F" in keys(pg), "建物追加で 1F が自動でできる: %s" % keys(pg))
    ck(floors(pg) == ["1F"], "新しい建物の階リストは 1F だけ: %s" % floors(pg))
    added = pg.evaluate("window.__kv.get('本社ビル/1F')")
    ck(len(added) < 1000, "利用者が足した建物は白紙から: %d バイト" % len(added))

    print("-- 自動保存 --")
    before = pg.evaluate("window.__kv.get('本社ビル/1F')")
    pg.evaluate("window.__log = []")
    pg.click("#m1")                      # カメラ追加モード
    pg.mouse.click(500, 400)             # カメラを1台置く
    pg.wait_for_timeout(400)
    ck(pg.evaluate("window.__log.length") == 0, "編集直後はまだ保存していない（待つ）")
    pg.wait_for_timeout(2200)            # AUTOSAVE_IDLE_MS = 1500
    log = pg.evaluate("window.__log")
    ck(any(x == "save:本社ビル/1F" for x in log), "編集が止まってから自動保存された: %s" % log)
    after = pg.evaluate("window.__kv.get('本社ビル/1F')")
    ck(after != before, "保存内容が更新されている")
    ck("カメラ" not in msg(pg) and "保存しました" in msg(pg), "保存メッセージ: %s" % msg(pg))

    print("-- 何も変えなければ保存しない --")
    pg.evaluate("window.__log = []")
    pg.mouse.move(600, 300)
    pg.wait_for_timeout(2200)
    ck(not any(x.startswith("save:") for x in pg.evaluate("window.__log")),
       "変化が無ければ書き戻さない: %s" % pg.evaluate("window.__log"))

    print("-- 建物を切り替えると図面も入れ替わる --")
    pg.select_option("#bldSel", "サンプル株式会社")
    pg.wait_for_timeout(1500)
    ck(sorted(floors(pg)) == ["1F", "2F"], "切替先の階リストになる: %s" % floors(pg))
    n_after = pg.eval_on_selector_all("#camList .chip", "e=>e.length")
    ck(n_after == 0, "切替先の階にはカメラが無い（図面が入れ替わった）: %d" % n_after)
    pg.select_option("#bldSel", "本社ビル")
    pg.wait_for_timeout(1500)
    n_back = pg.eval_on_selector_all("#camList .chip", "e=>e.length")
    ck(n_back == 1, "戻すと置いたカメラが復元される: %d" % n_back)

    print("-- 最後の階は消せない --")
    pg.evaluate("window.confirm = () => true")
    pg.click("#flrDel")
    pg.wait_for_timeout(600)
    ck("最後の階は削除できません" in msg(pg), "メッセージ: %s" % msg(pg))
    ck("本社ビル/1F" in keys(pg), "消えていない")

    print("-- 階を削除 --")
    pg.select_option("#bldSel", "サンプル株式会社")
    pg.wait_for_timeout(1500)
    pg.click("#flrList .row:has-text('2F')")
    pg.wait_for_timeout(1200)
    pg.click("#flrDel")
    pg.wait_for_timeout(1500)
    ck(floors(pg) == ["1F"], "2F が消えて 1F だけになる: %s" % floors(pg))
    ck("サンプル株式会社/2F" not in keys(pg), "KV からも消える: %s" % keys(pg))
    ck(all(not k.endswith("/null") and not k.endswith("/undefined") for k in keys(pg)),
       "削除直後に変なキーを作っていない: %s" % keys(pg))

    print("-- 建物を削除（階ごと）--")
    pg.evaluate("window.prompt = () => '3F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(1200)
    ck(len(floors(pg)) == 2, "削除テスト用に 3F を追加: %s" % floors(pg))
    pg.click("#bldDel")
    pg.wait_for_timeout(2000)
    ck(buildings(pg) == ["本社ビル"], "建物が消える: %s" % buildings(pg))
    ck([k for k in keys(pg) if "/" in k] == ["本社ビル/1F"],
       "配下の階も全部消える: %s" % keys(pg))

    print("-- 最後の建物は消せない --")
    pg.click("#bldDel")
    pg.wait_for_timeout(600)
    ck("最後の建物は削除できません" in msg(pg), "メッセージ: %s" % msg(pg))
    ck([k for k in keys(pg) if "/" in k] == ["本社ビル/1F"], "消えていない")

    print("-- 階ごとの緯度・経度（図面の JSON に入る）--")
    import json as _json

    def floor_json(key):
        return _json.loads(pg.evaluate("window.__kv.get(%s)" % _json.dumps(key)))

    ck(pg.input_value("#bldLat") == "" and pg.input_value("#bldLon") == "",
       "最初は未設定")
    pg.fill("#bldLat", "35.6812345")
    pg.locator("#bldLat").press("Tab")
    pg.wait_for_timeout(400)
    pg.fill("#bldLon", "139.7671248")
    pg.locator("#bldLon").press("Tab")
    pg.wait_for_timeout(2500)                  # AUTOSAVE_IDLE_MS = 1500
    j = floor_json("本社ビル/1F")
    ck(j.get("lat") == 35.6812345, "図面の JSON に緯度が入る: %s" % j.get("lat"))
    ck(j.get("lon") == 139.7671248, "経度も: %s" % j.get("lon"))
    ck("__camplan_buildings__" not in pg.evaluate("[...window.__kv.keys()]"),
       "予約キーはもう作らない")

    print("-- 階ごとに別々 --")
    pg.evaluate("window.prompt = () => '9F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(2000)
    ck(pg.input_value("#bldLat") == "", "新しい階は未設定: %s" % pg.input_value("#bldLat"))
    pg.fill("#bldLat", "35.7")
    pg.locator("#bldLat").press("Tab")
    pg.wait_for_timeout(2500)
    ck(floor_json("本社ビル/9F").get("lat") == 35.7, "9F の緯度")
    ck(floor_json("本社ビル/1F").get("lat") == 35.6812345, "1F は変わらない")
    pg.click("#flrList .row:has-text('1F')")
    pg.wait_for_timeout(2000)
    ck(pg.input_value("#bldLat") == "35.6812345",
       "1F に戻すと 1F の値: %s" % pg.input_value("#bldLat"))
    ck(pg.input_value("#bldLon") == "139.7671248",
       "経度も戻る: %s" % pg.input_value("#bldLon"))

    print("-- 階を消せば中身ごと消える --")
    pg.click("#flrList .row:has-text('9F')")
    pg.wait_for_timeout(2000)
    pg.evaluate("window.confirm = () => true")
    pg.click("#flrDel")
    pg.wait_for_timeout(2000)
    ck("本社ビル/9F" not in pg.evaluate("[...window.__kv.keys()]"), "9F が消える")

    print("-- 範囲外は拒否 --")
    pg.fill("#bldLat", "100")
    pg.locator("#bldLat").press("Tab")
    pg.wait_for_timeout(700)
    ck("-90" in msg(pg), "メッセージ: %s" % msg(pg))
    ck(pg.input_value("#bldLat") == "35.6812345",
       "元の値に戻る: %s" % pg.input_value("#bldLat"))
    pg.fill("#bldLon", "abc")
    pg.locator("#bldLon").press("Tab")
    pg.wait_for_timeout(700)
    ck("-180" in msg(pg), "経度も: %s" % msg(pg))

    print("-- 空にすると未設定に戻る --")
    pg.fill("#bldLat", "")
    pg.locator("#bldLat").press("Tab")
    pg.wait_for_timeout(2500)
    j = floor_json("本社ビル/1F")
    ck("lat" not in j, "緯度が JSON から消える: %s" % list(j.keys()))
    ck(j.get("lon") == 139.7671248, "経度は残る: %s" % j.get("lon"))

    print("-- 名前のチェック --")
    pg.evaluate("window.prompt = () => 'a/b'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(500)
    ck("/ は使えません" in msg(pg), "スラッシュ入りの名前を拒否: %s" % msg(pg))
    pg.evaluate("window.prompt = () => '   '")
    pg.click("#bldAdd")
    pg.wait_for_timeout(500)
    ck("名前を入力してください" in msg(pg), "空の名前を拒否: %s" % msg(pg))

    ck(errs == [], "JS エラーなし %s" % errs)
    pg.screenshot(path=os.path.join(HERE, "camplan_nav.png"), full_page=False)
    ctx.close()

    # ------------------------------------------------ 起動時に読み直す
    print("== B2. 図面を読み直すと緯度経度も戻る ==")
    ctx = b.new_context(viewport={"width": 1280, "height": 860})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(MOCK + """
    window.__kv.set('A社/1F', '{"app":"camplan","version":1,"lat":35.5,"lon":139.5,'
                            + '"walls":[],"cameras":[]}');
    window.__kv.set('A社/2F', '{"app":"camplan","version":1,"lat":35.6,"lon":139.6,'
                            + '"walls":[],"cameras":[]}');
    window.__kv.set('B社/1F', '{"app":"camplan","version":1,"walls":[],"cameras":[]}');
    """)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3000)
    ck(pg.input_value("#bldLat") == "35.5", "緯度が読める: %s" % pg.input_value("#bldLat"))
    ck(pg.input_value("#bldLon") == "139.5", "経度が読める: %s" % pg.input_value("#bldLon"))
    pg.click("#flrList .row:has-text('2F')")
    pg.wait_for_timeout(2000)
    ck(pg.input_value("#bldLat") == "35.6" and pg.input_value("#bldLon") == "139.6",
       "階を切り替えるとその階の値: %s / %s"
       % (pg.input_value("#bldLat"), pg.input_value("#bldLon")))
    pg.select_option("#bldSel", "B社")
    pg.wait_for_timeout(2000)
    ck(pg.input_value("#bldLat") == "" and pg.input_value("#bldLon") == "",
       "未設定の階では空になる: %s / %s"
       % (pg.input_value("#bldLat"), pg.input_value("#bldLon")))
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    # ---------------------------------------------------- ストレージが壊れている
    print("== C. camplanStorage が失敗するとき ==")
    ctx = b.new_context(viewport={"width": 1280, "height": 860})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(
        "window.camplanStorage = { list: () => Promise.reject(new Error('boom')),"
        " load:()=>0, save:()=>0, remove:()=>0 };")
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(2500)
    # 黙って消すと「機能が無い」のか「繋がっていない」のか分からないので、
    # パネルは出したまま理由を書く（保存はしないので単体動作と同じ）
    ck(pg.eval_on_selector("#navBox", "e=>getComputedStyle(e).display") != "none",
       "パネルは出したままにする")
    ck("保存先に接続できません" in msg(pg), "理由が書いてある: %s" % msg(pg))
    ck("boom" in msg(pg), "元の例外の文言も出る: %s" % msg(pg))
    ck(pg.eval_on_selector("#navMsg", "e=>e.className") == "err", "エラー色")
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()
    b.close()

print("")
print("%d failure(s)" % len(fails))
for f in fails:
    print("  -", f)
sys.exit(1 if fails else 0)

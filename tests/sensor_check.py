"""② センサー。四角いエリアの追加・選択・移動・サイズ変更・削除と、
ダブルクリックの赤（保存される）。

  (cd docs && python -m http.server 8123 &)
  python tests/sensor_check.py http://127.0.0.1:8123/
"""
import json
import os
import sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8123/"

KV = """
window.__kv = new Map();
window.camplanStorage = { readOnly:false,
  list:()=>Promise.resolve([...window.__kv].map(([k,v])=>({key:k,size:v.length}))),
  load:(k)=>Promise.resolve(window.__kv.get(k)),
  save:(k,j)=>{window.__kv.set(k,j);return Promise.resolve();},
  remove:(k)=>{window.__kv.delete(k);return Promise.resolve();} };
"""

fails = []


def ck(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def saved(pg):
    """自動保存された図面の JSON"""
    pg.wait_for_timeout(2300)          # AUTOSAVE_IDLE_MS = 1500
    return json.loads(pg.evaluate("[...window.__kv.values()][0]"))


def drag(pg, x0, y0, x1, y1):
    pg.mouse.move(x0, y0)
    pg.mouse.down()
    pg.mouse.move((x0 + x1) / 2, (y0 + y1) / 2, steps=4)
    pg.mouse.move(x1, y1, steps=4)
    pg.mouse.up()
    pg.wait_for_timeout(500)


def sensors(pg):
    return pg.evaluate("window.__M._cp_sensor_count()")


with sync_playwright() as p:
    b = p.chromium.launch(headless=True)
    ctx = b.new_context(viewport={"width": 1500, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(KV)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3000)

    print("== 1. ツールバーに出ている ==")
    ck(pg.eval_on_selector("#m4", "e=>e.textContent") == "センサー追加",
       "ボタンがある")
    ck(pg.eval_on_selector("#senBox", "e=>getComputedStyle(e).display") == "none",
       "最初はパネルが出ない")

    print("== 2. ドラッグで四角いエリアを置く ==")
    pg.click("#m4")
    drag(pg, 500, 350, 700, 480)
    j = saved(pg)
    ck(len(j.get("sensors", [])) == 1, "1個できる: %s" % j.get("sensors"))
    s = j["sensors"][0]
    ck(s["x1"] > s["x0"] and s["y1"] > s["y0"], "左上<右下に正規化される: %s" % s)
    ck(not s.get("alert"), "最初は通常（alert は書かれない）")
    ck(pg.eval_on_selector("#senBox", "e=>getComputedStyle(e).display") != "none",
       "置いた直後は選択されてパネルが出る")
    ck(pg.eval_on_selector("#senState", "e=>e.textContent") == "通常",
       "状態の表示: %s" % pg.eval_on_selector("#senState", "e=>e.textContent"))
    ck(s.get("label") == "A", "記号は A から: %s" % s.get("label"))
    ck(pg.input_value("#senLabel") == "A", "パネルにも出る")

    print("== 2a. ドラッグせずに離しても潰れない ==")
    # 1ドットのエリアは当たり判定に入らず、選ぶことも消すこともできなくなる
    pg.click("#m4")
    pg.mouse.click(1050, 300)
    pg.wait_for_timeout(700)
    j = saved(pg)
    s2 = j["sensors"][-1]
    ck((s2["x1"] - s2["x0"]) > 24 and (s2["y1"] - s2["y0"]) > 24,
       "既定の大きさになる: %.0f x %.0f" % (s2["x1"] - s2["x0"], s2["y1"] - s2["y0"]))
    ck(pg.eval_on_selector("#m0", "e=>e.className").find("on") >= 0,
       "置いたら選択モードに戻る")
    # 消せることまで見る
    pg.click("#m3")
    pg.mouse.click(1050, 300)
    pg.wait_for_timeout(700)
    ck(len(saved(pg).get("sensors", [])) == 1, "ちゃんと消せる")
    pg.click("#m0")
    pg.mouse.click(600, 415)
    pg.wait_for_timeout(600)

    print("== 2b. 2個目は B、記号は変えられる ==")
    pg.click("#m4")
    drag(pg, 820, 560, 980, 660)
    j = saved(pg)
    ck([x.get("label") for x in j["sensors"]] == ["A", "B"],
       "順に振られる: %s" % [x.get("label") for x in j["sensors"]])
    alerts = []
    pg.on("dialog", lambda dlg: (alerts.append(dlg.message), dlg.dismiss()))
    pg.fill("#senLabel", "A")          # 既に使われている
    pg.locator("#senLabel").press("Tab")
    pg.wait_for_timeout(600)
    ck(any("A〜Z" in a for a in alerts), "重複は断る: %s" % alerts)
    ck(saved(pg)["sensors"][1]["label"] == "B", "元のまま")
    pg.fill("#senLabel", "k")          # 小文字でも通る
    pg.locator("#senLabel").press("Tab")
    pg.wait_for_timeout(600)
    ck(saved(pg)["sensors"][1]["label"] == "K",
       "大文字にして受ける: %s" % saved(pg)["sensors"][1]["label"])
    # 後の確認のため元に戻して2個目は消す
    pg.click("#m3")
    pg.mouse.click(900, 610)
    pg.wait_for_timeout(600)
    pg.click("#m0")
    pg.mouse.click(600, 415)
    pg.wait_for_timeout(600)

    print("== 3. 名前・デバイスID・APIキー・コメント ==")
    ins = pg.locator("#senKv input")
    ck(ins.count() == 4, "4項目: %d" % ins.count())
    labels = pg.eval_on_selector_all("#senKv label", "e=>e.map(x=>x.textContent)")
    ck(labels == ["名前", "デバイスID", "APIキー", "コメント"], "ラベル: %s" % labels)
    for n, v in enumerate(["入口センサー", "sen-001", "skey-xyz", "ドア横"]):
        ins.nth(n).fill(v)
        ins.nth(n).press("Tab")
        pg.wait_for_timeout(200)
    j = saved(pg)
    ck(j["sensors"][0].get("props") ==
       {"name": "入口センサー", "device_id": "sen-001",
        "api_key": "skey-xyz", "comment": "ドア横"},
       "保存される: %s" % j["sensors"][0].get("props"))

    print("== 4. ダブルクリックで赤、もう一度で戻る ==")
    pg.click("#m0")
    pg.mouse.dblclick(600, 415)
    pg.wait_for_timeout(600)
    ck(pg.eval_on_selector("#senState", "e=>e.textContent") == "異常（赤）",
       "赤になる: %s" % pg.eval_on_selector("#senState", "e=>e.textContent"))
    j = saved(pg)
    ck(j["sensors"][0].get("alert") is True, "JSON に保存される: %s" % j["sensors"][0])
    pg.mouse.dblclick(600, 415)
    pg.wait_for_timeout(600)
    ck(pg.eval_on_selector("#senState", "e=>e.textContent") == "通常", "戻る")
    j = saved(pg)
    ck("alert" not in j["sensors"][0], "通常なら alert を書かない")

    print("== 5. パネルのボタンでも切り替わる ==")
    pg.click("#senAlertBtn")
    pg.wait_for_timeout(500)
    ck(pg.eval_on_selector("#senState", "e=>e.textContent") == "異常（赤）",
       "ボタンで赤になる")
    ck(pg.eval_on_selector("#senAlertBtn", "e=>e.textContent") == "異常を解除",
       "ボタンの文字が変わる")
    pg.click("#senAlertBtn")
    pg.wait_for_timeout(500)

    print("== 6. ページにイベントが飛ぶ ==")
    pg.evaluate("""() => {
        window.__ev = [];
        window.addEventListener('camplan:sensor', e => window.__ev.push(e.detail));
        window.__hook = [];
        window.onSensorToggle = (i, on, p) => window.__hook.push([i, on, p]);
    }""")
    pg.mouse.dblclick(600, 415)
    pg.wait_for_timeout(600)
    ev = pg.evaluate("window.__ev")
    ck(len(ev) == 1 and ev[0]["alert"] is True, "camplan:sensor が飛ぶ: %s" % ev)
    ck(ev[0]["props"]["name"] == "入口センサー", "props が入る: %s" % ev[0]["props"])
    hook = pg.evaluate("window.__hook")
    ck(len(hook) == 1 and hook[0][1] is True, "onSensorToggle も呼ばれる: %s" % hook)
    pg.mouse.dblclick(600, 415)
    pg.wait_for_timeout(600)

    print("== 7. 動かす ==")
    before = saved(pg)["sensors"][0]
    drag(pg, 600, 415, 660, 455)
    after = saved(pg)["sensors"][0]
    ck(abs((after["x1"] - after["x0"]) - (before["x1"] - before["x0"])) < 1,
       "大きさは変わらない")
    ck(after["x0"] > before["x0"] and after["y0"] > before["y0"],
       "位置が動く: %s -> %s" % (before["x0"], after["x0"]))

    print("== 8. 角でサイズを変える ==")
    s = saved(pg)["sensors"][0]
    # 右下の角の画面座標を出してから掴む
    corner = pg.evaluate("""(s) => {
        // world -> screen は内部なので、キャンバスの矩形から逆算せずに
        // 「右下あたり」をクリックして当たるかで見る
        return s;
    }""", s)
    w0 = s["x1"] - s["x0"]
    pos = pg.evaluate("""() => {
        const r = document.getElementById('view').getBoundingClientRect();
        return [r.left, r.top];
    }""")
    # 直前のドラッグで右下は (660+100, 455+65) 付近
    drag(pg, 760, 520, 860, 600)
    s2 = saved(pg)["sensors"][0]
    ck((s2["x1"] - s2["x0"]) > w0, "横が広がる: %.0f -> %.0f" % (w0, s2["x1"] - s2["x0"]))

    print("== 9. 消去モードで消える ==")
    pg.click("#m3")
    pg.mouse.click(700, 500)
    pg.wait_for_timeout(600)
    j = saved(pg)
    ck(not j.get("sensors"), "消える: %s" % j.get("sensors"))
    ck(pg.eval_on_selector("#senBox", "e=>getComputedStyle(e).display") == "none",
       "パネルも引っ込む")

    print("== 10. 元に戻せる ==")
    pg.click("#m0")
    pg.eval_on_selector("#view", "e=>e.focus()")
    pg.keyboard.press("Control+z")
    pg.wait_for_timeout(800)
    j = saved(pg)
    ck(len(j.get("sensors", [])) == 1, "undo で戻る: %s" % j.get("sensors"))

    ck(errs == [], "JS エラーなし %s" % errs)
    pg.screenshot(path=os.path.join(HERE, "camplan_sensor.png"))
    ctx.close()

    print("== 11. 保存された図面を読み直すと復元される ==")
    # モックの Map はリロードで作り直されるので、種を仕込んだ別の窓で見る。
    # 1個だけ置けば zoomToFit がそれを中央に収めるので、画面中央で掴める。
    SEED = KV + """
    window.__kv.set('A社/1F', JSON.stringify({
      app: 'camplan', version: 1, marker: 16, walls: [], cameras: [],
      sensors: [{ x0: 100, y0: 80, x1: 300, y1: 200, alert: true,
                  props: { name: '受付センサー', device_id: 'sen-9' } }]
    }));
    """
    ctx = b.new_context(viewport={"width": 1500, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(SEED)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3500)
    j = json.loads(pg.evaluate("window.__kv.get('A社/1F')"))
    ck(len(j["sensors"]) == 1 and j["sensors"][0]["alert"] is True,
       "赤の状態ごと読める: %s" % j["sensors"][0])

    box = pg.evaluate("""() => {
        const r = document.getElementById('view').getBoundingClientRect();
        return [r.left + r.width / 2, r.top + r.height / 2];
    }""")
    pg.click("#m0")
    pg.mouse.click(box[0], box[1])
    pg.wait_for_timeout(900)
    shown = pg.eval_on_selector("#senBox", "e=>getComputedStyle(e).display")
    ck(shown != "none", "クリックで選べる（パネルが出る）: %s" % shown)
    vals = pg.eval_on_selector_all("#senKv input", "e=>e.map(x=>x.value)")
    ck(vals and vals[0] == "受付センサー", "名前が出る: %s" % vals)
    ck(pg.eval_on_selector("#senState", "e=>e.textContent") == "異常（赤）",
       "赤のまま復元される")

    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    print("== 11b. 潰れたセンサーは読み込みで直す ==")
    # 過去に作ってしまった 1 ドットのエリアは掴めない（選べないので消せない）。
    # 読み込みのときに最小の大きさまで広げて救う。
    BAD = KV + """
    window.__kv.set('A社/1F', JSON.stringify({
      app: 'camplan', version: 1, marker: 16, walls: [], cameras: [],
      sensors: [{ x0: 100, y0: 80,  x1: 300, y1: 200 },
                { x0: 500, y0: 500, x1: 500, y1: 500 },
                { x0: 600, y0: 600, x1: 602, y1: 601 }]
    }));
    """
    ctx = b.new_context(viewport={"width": 1500, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(BAD)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3500)
    # 何か触れば自動保存が直したものを書き戻す
    pg.evaluate("window.prompt = () => '9F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(2500)
    pg.click("#flrList .row:has-text('1F')")
    pg.wait_for_timeout(3000)
    j2 = json.loads(pg.evaluate("window.__kv.get('A社/1F')"))
    sizes = [(round(s["x1"] - s["x0"]), round(s["y1"] - s["y0"])) for s in j2["sensors"]]
    ck(all(w >= 24 and h >= 24 for w, h in sizes),
       "全部つかめる大きさになる: %s" % sizes)
    ck(sizes[0] == (200, 120), "元から大きいものは変わらない: %s" % (sizes[0],))
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()
    b.close()

print("")
print("%d failure(s)" % len(fails))
for f in fails:
    print("  -", f)
sys.exit(1 if fails else 0)

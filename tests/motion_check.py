"""③ 直近10分にモーションのあったカメラの色が変わるか。

Safie SDK はモックに差し替える。本物で確かめたこと: start/end はミリ秒、
end は1分ほど過去でないと「Invalid end」で弾かれる、範囲は最大24時間、
types は Safie.Devices.StandardEvent の値（motion / person / sound ...）。

  (cd docs && python -m http.server 8123 &)
  python tests/motion_check.py http://127.0.0.1:8123/
"""
import os
import sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8123/"

SDK_MOCK = """
window.__safie = { events: [], motionFor: {}, fail: null, tokens: [] };
window.__timers = [];
(function (orig) {
  window.setInterval = function (fn, ms) {
    window.__timers.push({ ms: ms, fn: fn });
    return orig.call(window, fn, ms);
  };
})(window.setInterval);
window.Safie = {
  Auth: { setToken: function (k, kind) {
    window.__safie.tokens.push([k, kind]); return Promise.resolve(); } },
  Devices: {
    StandardEvent: { Motion: 'motion' },
    queryThumbnail: function () {
      return Promise.resolve(new Blob([new Uint8Array([0xFF, 0xD8])],
                                      { type: 'image/jpeg' }));
    },
    queryStandardEvents: function (a) {
      window.__safie.events.push(a);
      if (window.__safie.fail) return Promise.reject(new Error(window.__safie.fail));
      var n = window.__safie.motionFor[a.deviceId] ? 1 : 0;
      return Promise.resolve({ total: n, offset: 0, list: [] });
    }
  },
  Player: { StreamingPlayer: function () {
    this.deviceId = null;
    this.play = function () { return Promise.resolve(); };
    this.stop = function () { };
  } }
};
"""

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


def tick(pg):
    """1分ごとのタイマーを手で1回まわす"""
    pg.evaluate("""() => {
        const t = window.__timers.find(t => t.ms === 60000);
        return t && t.fn();
    }""")
    pg.wait_for_timeout(1500)


def place(pg, x, y, props):
    pg.click("#m1")
    pg.mouse.click(x, y)
    pg.wait_for_timeout(600)
    ins = pg.locator("#pKv input")
    for n, key in enumerate(["name", "device_id", "api_key", "comment"]):
        ins.nth(n).fill(props.get(key, ""))
        ins.nth(n).press("Tab")
        pg.wait_for_timeout(150)
    pg.wait_for_timeout(300)


with sync_playwright() as p:
    b = p.chromium.launch(headless=True)
    ctx = b.new_context(viewport={"width": 1500, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(SDK_MOCK)
    pg.add_init_script(KV)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3000)

    print("== 1. 資格情報のあるカメラだけ問い合わせる ==")
    place(pg, 500, 380, {"name": "入口", "device_id": "dev-1", "api_key": "key-1"})
    place(pg, 760, 300, {"name": "裏口", "device_id": "dev-2", "api_key": "key-2"})
    place(pg, 980, 500, {"name": "物置"})          # 資格情報なし
    pg.click("#m0")
    pg.evaluate("window.__safie.events = []")
    tick(pg)
    asked = sorted(e["deviceId"] for e in pg.evaluate("window.__safie.events"))
    ck(asked == ["dev-1", "dev-2"], "問い合わせ先: %s" % asked)

    print("== 2. 問い合わせの中身 ==")
    ev = pg.evaluate("window.__safie.events")[0]
    ck(ev["types"] == ["motion"], "motion だけ: %s" % ev.get("types"))
    now = pg.evaluate("Date.now()")
    # 「直近10分」= start が10分前。end は1分前までしか遡れない（Safie が
    # 直近を弾くため）ので、実際に見える幅は9分になる。
    back = now - ev["start"]
    ck(abs(back - 10 * 60 * 1000) < 3000, "start は10分前: %d ms" % back)
    lag = now - ev["end"]
    ck(55000 < lag < 70000, "end は約1分前（Safie が未来を弾くため）: %d ms" % lag)
    ck(abs((ev["end"] - ev["start"]) - 9 * 60 * 1000) < 3000,
       "見える幅は9分: %d ms" % (ev["end"] - ev["start"]))
    ck(ev["start"] > 1_600_000_000_000, "ミリ秒で渡す: %s" % ev["start"])

    print("== 3. 動きがあれば色が変わる ==")
    pg.evaluate("window.__safie.motionFor = {'dev-1': true}")
    tick(pg)
    shot_on = pg.locator("#view").screenshot()
    pg.evaluate("window.__safie.motionFor = {}")
    tick(pg)
    shot_off = pg.locator("#view").screenshot()
    ck(shot_on != shot_off, "動きの有無で描画が変わる")

    print("== 4. 取れなければ動きなしに倒す ==")
    pg.evaluate("window.__safie.motionFor = {'dev-1': true}")
    tick(pg)
    on_again = pg.locator("#view").screenshot()
    ck(on_again == shot_on, "もう一度 ON で同じ絵に戻る")
    pg.evaluate("window.__safie.fail = 'offline'")
    tick(pg)
    ck(pg.locator("#view").screenshot() == shot_off,
       "エラーなら消える（取れないことを赤で見せない）")
    pg.evaluate("window.__safie.fail = null")
    ck(errs == [], "JS エラーなし %s" % errs)

    print("== 5. 保存される JSON には入らない ==")
    pg.evaluate("window.__safie.motionFor = {'dev-1': true}")
    tick(pg)
    pg.wait_for_timeout(2300)
    saved = pg.evaluate("[...window.__kv.values()][0]")
    ck("motion" not in saved, "motion は保存されない")

    print("== 6. プレーヤーを開いている間は問い合わせない ==")
    pg.mouse.dblclick(500, 380)
    pg.wait_for_timeout(1200)
    pg.evaluate("window.__safie.events = []")
    tick(pg)
    ck(pg.evaluate("window.__safie.events") == [],
       "setToken を奪わない: %s" % pg.evaluate("window.__safie.events"))
    pg.click("#playerClose")
    pg.wait_for_timeout(2000)
    ck(len(pg.evaluate("window.__safie.events")) > 0, "閉じたら取り直す")

    print("== 7. 階を変えたら持ち越さない ==")
    pg.evaluate("window.prompt = () => '2F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(2500)
    ck(pg.eval_on_selector_all("#camList .chip", "e=>e.length") == 0,
       "新しい階にはカメラが無い")
    blank = pg.locator("#view").screenshot()
    pg.click("#flrList .row:has-text('1F')")
    pg.wait_for_timeout(2500)
    ck(blank != pg.locator("#view").screenshot(), "戻すと元の階が出る")

    ck(errs == [], "JS エラーなし %s" % errs)
    pg.screenshot(path=os.path.join(HERE, "camplan_motion.png"))
    ctx.close()
    b.close()

print("")
print("%d failure(s)" % len(fails))
for f in fails:
    print("  -", f)
sys.exit(1 if fails else 0)

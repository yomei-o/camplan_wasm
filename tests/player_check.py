"""カメラの固定プロパティ4項目と Safie プレーヤー起動をヘッドレスで検証する。

Safie SDK は実機がないと再生できないので、SDK をモックに差し替えて
「正しい引数で呼ばれるか」「エラーが画面に出るか」を見る。要 Playwright。

  (cd docs && python -m http.server 8123 &)
  python tests/player_check.py http://127.0.0.1:8123/
"""
import os
import sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8123/"

# Safie SDK のモック。script の onload より先に window.Safie を置いておくと
# loadSafieSdk() が即 resolve するので、外部への通信は起きない。
SDK_MOCK = """
window.__safie = { setToken: [], deviceId: null, played: 0, stopped: 0 };
window.__safieFail = null;
window.Safie = {
  Auth: {
    setToken: function (key, kind) {
      window.__safie.setToken.push([key, kind]);
      return Promise.resolve();
    }
  },
  Player: {
    StreamingPlayer: function (el) {
      window.__safie.el = el && el.id;
      var self = this;
      this.deviceId = null;
      this.play = function (t) {
        window.__safie.deviceId = self.deviceId;
        window.__safie.playedAt = t;
        window.__safie.played++;
        if (window.__safieFail) return Promise.reject(new Error(window.__safieFail));
        return Promise.resolve();
      };
      this.stop = function () { window.__safie.stopped++; };
    }
  }
};
"""

KV_MOCK = """
window.__kv = new Map();
window.camplanStorage = {
  list:   ()          => Promise.resolve([...window.__kv].map(([k, v]) => ({key: k, size: v.length}))),
  load:   (key)       => Promise.resolve(window.__kv.get(key)),
  save:   (key, json) => { window.__kv.set(key, json); return Promise.resolve(); },
  remove: (key)       => { window.__kv.delete(key); return Promise.resolve(); },
};
"""

fails = []


def ck(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def labels(pg):
    return pg.eval_on_selector_all("#pKv label", "e=>e.map(x=>x.innerText)")


def values(pg):
    return pg.eval_on_selector_all("#pKv input", "e=>e.map(x=>x.value)")


def props_of(pg, no):
    """自動保存された JSON から、そのカメラの props を読む"""
    pg.wait_for_timeout(2200)              # AUTOSAVE_IDLE_MS = 1500
    return pg.evaluate(
        "(no) => { const j = [...window.__kv.values()].map(JSON.parse)"
        "            .find(d => (d.cameras||[]).some(c => c.no === no));"
        "          return j ? (j.cameras.find(c => c.no === no).props || {}) : null; }", no)


with sync_playwright() as p:
    b = p.chromium.launch(headless=True)
    ctx = b.new_context(viewport={"width": 1280, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(SDK_MOCK)
    pg.add_init_script(KV_MOCK)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(2500)

    print("== 1. プロパティは固定4項目 ==")
    pg.click("#m1")
    pg.mouse.click(500, 420)          # カメラ1台
    pg.wait_for_timeout(800)
    ck(labels(pg) == ["名前", "デバイスID", "APIキー", "コメント"],
       "ラベル: %s" % labels(pg))
    ck(len(values(pg)) == 4, "入力欄は4つ: %d" % len(values(pg)))
    ck(values(pg) == ["", "", "", ""], "初期値は空: %s" % values(pg))
    ck(pg.evaluate("!document.querySelector('#kvAdd')"), "「+ 行を追加」が無い")

    print("== 2. 入力すると props に入る ==")
    inputs = pg.locator("#pKv input")
    inputs.nth(0).fill("正面エントランス")
    inputs.nth(0).press("Tab")
    inputs.nth(1).fill("dev-123")
    inputs.nth(1).press("Tab")
    inputs.nth(2).fill("key-abc")
    inputs.nth(2).press("Tab")
    inputs.nth(3).fill("受付の上")
    inputs.nth(3).press("Tab")
    pg.wait_for_timeout(600)
    pr = props_of(pg, 1)
    ck(pr.get("name") == "正面エントランス", "name: %s" % pr.get("name"))
    ck(pr.get("device_id") == "dev-123", "device_id: %s" % pr.get("device_id"))
    ck(pr.get("api_key") == "key-abc", "api_key: %s" % pr.get("api_key"))
    ck(pr.get("comment") == "受付の上", "comment: %s" % pr.get("comment"))
    ck(sorted(pr.keys()) == ["api_key", "comment", "device_id", "name"],
       "余計なキーが無い: %s" % sorted(pr.keys()))

    print("== 4. ダブルクリックで Safie プレーヤーが開く ==")
    pg.evaluate("window.__safie.setToken = []; window.__safie.played = 0")
    pos = pg.evaluate("""() => {
        const r = document.getElementById('view').getBoundingClientRect();
        return [r.left, r.top];
    }""")
    pg.mouse.dblclick(500, 420)
    pg.wait_for_timeout(1200)
    ck(pg.eval_on_selector("#playerWrap", "e=>getComputedStyle(e).display") == "flex",
       "プレーヤーが開く")
    ck(pg.evaluate("window.__safie.setToken") == [["key-abc", "apiKey"]],
       "setToken(apiKey, 'apiKey'): %s" % pg.evaluate("window.__safie.setToken"))
    ck(pg.evaluate("window.__safie.deviceId") == "dev-123",
       "deviceId: %s" % pg.evaluate("window.__safie.deviceId"))
    ck(pg.evaluate("window.__safie.played") == 1, "play() が1回呼ばれた")
    ck("正面エントランス" in pg.inner_text("#playerTitle"),
       "タイトルに名前が出る: %s" % pg.inner_text("#playerTitle"))
    ck(pg.inner_text("#playerMsg") == "", "エラー表示なし")

    print("== 5. 閉じると stop が呼ばれる ==")
    pg.click("#playerClose")
    pg.wait_for_timeout(400)
    ck(pg.eval_on_selector("#playerWrap", "e=>getComputedStyle(e).display") == "none",
       "閉じる")
    ck(pg.evaluate("window.__safie.stopped") == 1, "stop() が呼ばれた")

    print("== 6. Esc でも閉じる ==")
    pg.mouse.dblclick(500, 420)
    pg.wait_for_timeout(1000)
    pg.keyboard.press("Escape")
    pg.wait_for_timeout(400)
    ck(pg.eval_on_selector("#playerWrap", "e=>getComputedStyle(e).display") == "none",
       "Esc で閉じる")

    print("== 7. 再生に失敗したら画面に出る ==")
    pg.evaluate("window.__safieFail = 'device offline'")
    pg.mouse.dblclick(500, 420)
    pg.wait_for_timeout(1200)
    ck("device offline" in pg.inner_text("#playerMsg"),
       "エラーメッセージ: %s" % pg.inner_text("#playerMsg"))
    ck(pg.eval_on_selector("#playerMsg", "e=>e.className") == "err", "エラー色")
    pg.keyboard.press("Escape")
    pg.evaluate("window.__safieFail = null")
    pg.wait_for_timeout(300)

    print("== 8. デバイスID / APIキーが無いカメラ ==")
    pg.click("#m1")
    pg.mouse.click(700, 300)          # カメラ2台目（プロパティ空）
    pg.wait_for_timeout(800)
    pg.click("#m0")
    alerts = []
    pg.on("dialog", lambda dlg: (alerts.append(dlg.message), dlg.dismiss()))
    pg.evaluate("window.__safie.played = 0")
    pg.mouse.dblclick(700, 300)
    pg.wait_for_timeout(1000)
    ck(pg.eval_on_selector("#playerWrap", "e=>getComputedStyle(e).display") == "none",
       "プレーヤーは開かない")
    ck(pg.evaluate("window.__safie.played") == 0, "play() は呼ばれない")
    ck(any("デバイスID" in a for a in alerts), "案内が出る: %s" % alerts)

    print("== 9. onCameraOpen を差せば従来どおり優先される ==")
    pg.evaluate("() => { window.__hook = [];"
                "        window.onCameraOpen = function (n, p) { window.__hook.push([n, p]); }; }")
    pg.evaluate("window.__safie.played = 0")
    pg.mouse.dblclick(500, 420)
    pg.wait_for_timeout(800)
    hook = pg.evaluate("window.__hook")
    ck(len(hook) == 1 and hook[0][0] == 1, "onCameraOpen が呼ばれる: %s" % hook)
    ck(hook[0][1].get("device_id") == "dev-123", "props が渡る")
    ck(pg.evaluate("window.__safie.played") == 0, "内蔵プレーヤーは動かない")
    ck(pg.eval_on_selector("#playerWrap", "e=>getComputedStyle(e).display") == "none",
       "オーバーレイも出ない")

    print("== 10. 階を往復しても残る（保存 -> 読み直し）==")
    pg.evaluate("window.prompt = () => '2F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(1500)
    pg.click("#flrList .row:has-text('1F')")
    pg.wait_for_timeout(1800)
    pg.click("#m0")
    pg.mouse.click(500, 420)
    pg.wait_for_timeout(900)
    ck(values(pg) == ["正面エントランス", "dev-123", "key-abc", "受付の上"],
       "読み直し後の表示: %s" % values(pg))

    ck(errs == [], "JS エラーなし %s" % errs)
    pg.evaluate("delete window.onCameraOpen")
    pg.mouse.dblclick(500, 420)
    pg.wait_for_timeout(1000)
    pg.screenshot(path=os.path.join(HERE, "camplan_player.png"))
    ctx.close()
    b.close()

print("")
print("%d failure(s)" % len(fails))
for f in fails:
    print("  -", f)
sys.exit(1 if fails else 0)

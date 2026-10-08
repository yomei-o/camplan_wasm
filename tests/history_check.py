"""① 視聴履歴。見たカメラがサムネイル付きで左に並ぶか。

Safie SDK はモックに差し替える（実機が無くても回るように）。本物の SDK で
確かめたこと: Devices.queryThumbnail は Blob を返す。要 Playwright。

  (cd docs && python -m http.server 8123 &)
  python tests/history_check.py http://127.0.0.1:8123/
"""
import os
import sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8123/"

# 1x1 の JPEG を Blob にして返すモック。setToken の履歴も残す。
SDK_MOCK = """
window.__safie = { tokens: [], thumbs: [], played: 0, fail: null };
// 1分ごとの更新を実測するため、setInterval を記録して手で回せるようにする
window.__timers = [];
(function (orig) {
  window.setInterval = function (fn, ms) {
    window.__timers.push({ ms: ms, fn: fn });
    return orig.call(window, fn, ms);
  };
})(window.setInterval);
function __jpeg(tag) {
  // 中身は何でもよいので、tag を混ぜて「取り直した」ことが分かるようにする
  const bytes = new Uint8Array([0xFF, 0xD8, 0xFF, 0xD9, tag & 0xFF]);
  return new Blob([bytes], { type: 'image/jpeg' });
}
window.Safie = {
  Auth: {
    setToken: function (key, kind) {
      window.__safie.tokens.push([key, kind]);
      return Promise.resolve();
    }
  },
  Devices: {
    StandardEvent: { Motion: 'motion' },
    queryThumbnail: function (a) {
      window.__safie.thumbs.push(a.deviceId);
      if (window.__safie.fail) return Promise.reject(new Error(window.__safie.fail));
      return Promise.resolve(__jpeg(window.__safie.thumbs.length));
    }
  },
  Player: {
    StreamingPlayer: function (el) {
      var self = this;
      this.deviceId = null;
      this.play = function () {
        window.__safie.played++;
        window.__safie.lastDevice = self.deviceId;
        return Promise.resolve();
      };
      this.stop = function () { };
    }
  }
};
"""

fails = []


def ck(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def items(pg):
    return pg.eval_on_selector_all("#histList .item .cap", "e=>e.map(x=>x.textContent)")


def shots(pg):
    return pg.eval_on_selector_all(
        "#histList .item", "e=>e.map(x=>{const i=x.querySelector('img.shot');"
        "return i ? i.src.slice(0,5) : 'none';})")


def place_camera(pg, x, y, props):
    """カメラを1台置いて、固定4項目を埋める"""
    pg.click("#m1")
    pg.mouse.click(x, y)
    pg.wait_for_timeout(600)
    inputs = pg.locator("#pKv input")
    for n, key in enumerate(["name", "device_id", "api_key", "comment"]):
        inputs.nth(n).fill(props.get(key, ""))
        inputs.nth(n).press("Tab")
        pg.wait_for_timeout(150)
    pg.wait_for_timeout(400)


with sync_playwright() as p:
    b = p.chromium.launch(headless=True)
    ctx = b.new_context(viewport={"width": 1500, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.add_init_script(SDK_MOCK)
    pg.goto(URL, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(2500)

    print("== 1. 最初は空 ==")
    ck(pg.eval_on_selector("#histPane", "e=>getComputedStyle(e).display") != "none",
       "履歴の枠は出ている")
    ck(items(pg) == [], "中身は空")
    ck(pg.eval_on_selector("#histNone", "e=>getComputedStyle(e).display") != "none",
       "案内が出ている")

    print("== 2. 見ると積まれる ==")
    place_camera(pg, 500, 400, {"name": "入口", "device_id": "dev-1", "api_key": "key-1"})
    pg.click("#m0")
    pg.mouse.dblclick(500, 400)
    pg.wait_for_timeout(1500)
    ck(items(pg) == ["カメラ 1 入口"], "1件目: %s" % items(pg))
    ck(pg.eval_on_selector("#histNone", "e=>getComputedStyle(e).display") == "none",
       "案内が消える")
    # 開いている間は取りに行かない（プレーヤーと setToken を取り合わないため）
    ck(pg.evaluate("window.__safie.thumbs") == [],
       "見ている間は取りに行かない: %s" % pg.evaluate("window.__safie.thumbs"))
    ck(shots(pg) == ["none"], "まだ画像なし: %s" % shots(pg))
    pg.click("#playerClose")
    pg.wait_for_timeout(1500)
    ck(pg.evaluate("window.__safie.thumbs") == ["dev-1"],
       "閉じたら取りに行く: %s" % pg.evaluate("window.__safie.thumbs"))
    ck(shots(pg) == ["blob:"], "blob の画像が貼られる: %s" % shots(pg))

    print("== 3. 別のカメラは別の行 ==")
    place_camera(pg, 760, 300, {"name": "裏口", "device_id": "dev-2", "api_key": "key-2"})
    pg.click("#m0")
    pg.mouse.dblclick(760, 300)
    pg.wait_for_timeout(1500)
    ck(items(pg) == ["カメラ 2 裏口", "カメラ 1 入口"],
       "新しい順に並ぶ: %s" % items(pg))
    pg.click("#playerClose")
    pg.wait_for_timeout(1500)
    ck(pg.evaluate("window.__safie.tokens").count(["key-2", "apiKey"]) >= 1,
       "そのカメラのAPIキーで取る")

    print("== 4. 同じカメラを見ても増えず、先頭へ来る ==")
    pg.mouse.dblclick(500, 400)
    pg.wait_for_timeout(1500)
    ck(items(pg) == ["カメラ 1 入口", "カメラ 2 裏口"],
       "積み上がらず先頭に移る: %s" % items(pg))
    pg.click("#playerClose")
    pg.wait_for_timeout(600)

    print("== 5. 履歴から押すともう一度見られる ==")
    pg.evaluate("window.__safie.played = 0")
    pg.click("#histList .item:nth-child(2)")      # 裏口
    pg.wait_for_timeout(1500)
    ck(pg.evaluate("window.__safie.played") == 1, "再生が走る")
    ck(pg.evaluate("window.__safie.lastDevice") == "dev-2",
       "そのカメラが選ばれる: %s" % pg.evaluate("window.__safie.lastDevice"))
    ck(items(pg)[0] == "カメラ 2 裏口", "押したものが先頭に来る: %s" % items(pg))
    pg.click("#playerClose")
    pg.wait_for_timeout(600)

    print("== 6. デバイスIDが無ければ画像なしで並ぶ ==")
    place_camera(pg, 980, 480, {"name": "物置"})
    pg.click("#m0")
    pg.mouse.dblclick(980, 480)
    pg.wait_for_timeout(1200)
    # 資格情報が無いので内蔵プレーヤーは開かず alert になる。履歴にも積まない
    ck(len(items(pg)) == 2, "資格情報が無いカメラは履歴に積まない: %s" % items(pg))

    print("== 7. 1分ごとに取り直す ==")
    timers = pg.evaluate("window.__timers.map(t => t.ms)")
    ck(60000 in timers, "60秒のタイマーが仕掛けられる: %s" % timers)
    pg.evaluate("window.__safie.thumbs = []")
    # タイマーの中身を手で1回だけ回す（60秒待たずに同じ経路を通す）
    pg.evaluate("""() => {
        const t = window.__timers.find(t => t.ms === 60000);
        return t && t.fn();
    }""")
    pg.wait_for_timeout(1200)
    got = pg.evaluate("window.__safie.thumbs")
    ck(sorted(got) == ["dev-1", "dev-2"], "全件を取り直す: %s" % got)
    ck(shots(pg) == ["blob:", "blob:"], "新しい画像に差し替わる: %s" % shots(pg))

    print("== 7b. プレーヤーを開いている間は取り直さない ==")
    pg.mouse.dblclick(500, 400)
    pg.wait_for_timeout(1200)
    pg.evaluate("window.__safie.thumbs = []")
    pg.evaluate("""() => {
        const t = window.__timers.find(t => t.ms === 60000);
        return t && t.fn();
    }""")
    pg.wait_for_timeout(800)
    ck(pg.evaluate("window.__safie.thumbs") == [],
       "setToken を奪わない: %s" % pg.evaluate("window.__safie.thumbs"))
    pg.click("#playerClose")
    pg.wait_for_timeout(1500)
    ck(len(pg.evaluate("window.__safie.thumbs")) > 0, "閉じたら取り直す")

    print("== 8. 取得に失敗しても落ちない ==")
    pg.evaluate("window.__safie.fail = 'offline'")
    pg.mouse.dblclick(500, 400)
    pg.wait_for_timeout(1500)
    ck(len(items(pg)) == 2, "履歴は壊れない: %s" % items(pg))
    ck(errs == [], "JS エラーなし %s" % errs)
    pg.click("#playerClose")

    pg.screenshot(path=os.path.join(HERE, "camplan_history.png"))
    ctx.close()
    b.close()

print("")
print("%d failure(s)" % len(fails))
for f in fails:
    print("  -", f)
sys.exit(1 if fails else 0)

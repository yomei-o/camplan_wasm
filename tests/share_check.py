"""一時URLの発行 / コピー / 通報 を、モックの camplanShare を差して確かめる。

camplan はセッションの作り方を知らないので、テストもページ側から差し込む。
要 Playwright。

  (cd docs && python -m http.server 8123 &)
  python tests/share_check.py http://127.0.0.1:8123/
"""
import os
import sys
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8123/"

# 保存先。readOnly はテストごとに差し替える。
KV = """
window.__kv = new Map();
window.__ro = %s;
window.camplanStorage = {
  readOnly: window.__ro,
  list:   ()          => Promise.resolve([...window.__kv].map(([k, v]) => ({key: k, size: v.length}))),
  load:   (key)       => Promise.resolve(window.__kv.get(key)),
  save:   (key, json) => { window.__kv.set(key, json); return Promise.resolve(); },
  remove: (key)       => { window.__kv.delete(key); return Promise.resolve(); },
};
"""

SHARE = """
window.__issued = 0;
window.__fail = null;
window.camplanShare = {
  issue: function () {
    window.__issued++;
    if (window.__fail) return Promise.reject(new Error(window.__fail));
    return Promise.resolve('https://example.test/html/camplan/index.html?session=tok-'
                           + window.__issued);
  }
};
"""

fails = []


def ck(cond, msg):
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def shown(pg, sel):
    return pg.eval_on_selector(sel, "e=>getComputedStyle(e).display") != "none"


def open_page(b, scripts, url=URL):
    ctx = b.new_context(viewport={"width": 1500, "height": 900})
    pg = ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    for s in scripts:
        pg.add_init_script(s)
    pg.goto(url, wait_until="load")
    pg.wait_for_selector("#view", timeout=60000)
    pg.wait_for_timeout(3000)
    return ctx, pg, errs


with sync_playwright() as p:
    b = p.chromium.launch(headless=True)

    print("== A. camplanShare が無いとき（単体動作）==")
    ctx, pg, errs = open_page(b, [])
    ck(not shown(pg, "#shareBox"), "共有のボタンは出ない")
    ck(not shown(pg, "#shareSep"), "区切りも出ない")
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    print("== B. 発行 ==")
    ctx, pg, errs = open_page(b, [KV % "false", SHARE])
    ck(shown(pg, "#shareBox"), "共有のボタンが出る")
    ck(pg.input_value("#shareUrl") == "", "最初は空")
    ck(pg.get_attribute("#shareUrl", "placeholder") == "未発行", "未発行と書いてある")
    pg.click("#shareIssue")
    pg.wait_for_timeout(800)
    url1 = pg.input_value("#shareUrl")
    ck("session=tok-1" in url1, "session が入る: %s" % url1)
    ck("building=%E3%82%B5%E3%83%B3%E3%83%97%E3%83%AB%E6%A0%AA%E5%BC%8F%E4%BC%9A%E7%A4%BE"
       in url1, "建物が入る: %s" % url1)
    ck("floor=1F" in url1, "階が入る: %s" % url1)
    ck(pg.evaluate("window.__issued") == 1, "issue() が1回呼ばれた")
    ck("発行しました" in pg.inner_text("#shareMsg"), "メッセージ: %s" % pg.inner_text("#shareMsg"))

    print("== C. 発行し直すと URL が変わる ==")
    pg.click("#shareIssue")
    pg.wait_for_timeout(800)
    ck("session=tok-2" in pg.input_value("#shareUrl"),
       "新しい URL: %s" % pg.input_value("#shareUrl"))

    print("== C2. 階を切り替えると URL も追従する ==")
    pg.evaluate("window.prompt = () => '3F'")
    pg.click("#flrAdd")
    pg.wait_for_timeout(1500)
    u = pg.input_value("#shareUrl")
    ck("floor=3F" in u and "session=tok-2" in u,
       "階だけ差し替わる（session はそのまま）: %s" % u)
    pg.click("#flrList .row:has-text('1F')")
    pg.wait_for_timeout(1500)
    ck("floor=1F" in pg.input_value("#shareUrl"),
       "戻すと 1F に: %s" % pg.input_value("#shareUrl"))

    print("== D. コピー ==")
    ctx.grant_permissions(["clipboard-read", "clipboard-write"])
    pg.click("#shareCopy")
    pg.wait_for_timeout(600)
    ck("コピーしました" in pg.inner_text("#shareMsg"), "メッセージ: %s" % pg.inner_text("#shareMsg"))
    got = pg.evaluate("navigator.clipboard.readText()")
    ck(got == pg.input_value("#shareUrl"), "クリップボードの中身: %s" % got)

    print("== E. 通報（既定は alert）==")
    alerts = []
    pg.on("dialog", lambda dlg: (alerts.append(dlg.message), dlg.dismiss()))
    pg.evaluate("() => { window.__ev = []; "
                "window.addEventListener('camplan:report', e => window.__ev.push(e.detail)); }")
    pg.click("#report")
    pg.wait_for_timeout(800)
    ck(len(alerts) == 1 and "tok-2" in alerts[0], "alert に URL が出る: %s" % alerts)
    ev = pg.evaluate("window.__ev")
    ck(len(ev) == 1, "camplan:report が飛ぶ: %s" % ev)
    ck(ev[0]["building"] == "サンプル株式会社" and ev[0]["floor"] == "1F",
       "建物と階が入る: %s" % ev[0])
    ck("通報しました" in pg.inner_text("#shareMsg"), "メッセージ: %s" % pg.inner_text("#shareMsg"))

    print("== F. onReport を差すと alert は出ない ==")
    pg.evaluate("() => { window.__hook = []; "
                "window.onReport = function (u, i) { window.__hook.push([u, i]); }; }")
    alerts.clear()
    pg.click("#report")
    pg.wait_for_timeout(800)
    hook = pg.evaluate("window.__hook")
    ck(len(hook) == 1 and "tok-2" in hook[0][0], "onReport が呼ばれる: %s" % hook)
    ck(alerts == [], "alert は出ない: %s" % alerts)
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    print("== G. 未発行のまま通報すると、先に発行する ==")
    ctx, pg, errs = open_page(b, [KV % "false", SHARE])
    pg.evaluate("() => { window.__hook = []; "
                "window.onReport = function (u, i) { window.__hook.push([u, i]); }; }")
    pg.click("#report")
    pg.wait_for_timeout(1000)
    ck(pg.evaluate("window.__issued") == 1, "発行が走る")
    ck("session=tok-1" in pg.input_value("#shareUrl"),
       "URL 欄にも入る: %s" % pg.input_value("#shareUrl"))
    ck(len(pg.evaluate("window.__hook")) == 1, "そのまま通報まで進む")

    print("== H. 発行に失敗したら理由が出る ==")
    pg.evaluate("window.__fail = 'create_temp_session が失敗'")
    pg.click("#shareIssue")
    pg.wait_for_timeout(800)
    ck("create_temp_session が失敗" in pg.inner_text("#shareMsg"),
       "メッセージ: %s" % pg.inner_text("#shareMsg"))
    ck(pg.eval_on_selector("#shareMsg", "e=>e.className") == "err", "エラー色")
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    print("== I. 一時URLで開いた人（readOnly）==")
    ctx, pg, errs = open_page(b, [KV % "true", SHARE], URL + "?session=tok-x")
    ck(not shown(pg, "#shareBox"), "さらに URL は配れない")
    ck(shown(pg, "#roTag"), "閲覧のみと出る")
    ck(shown(pg, "#navBox"), "建物/階のリストは見える")
    ck(not shown(pg, "#bldAdd") and not shown(pg, "#bldDel"), "建物の +/- は出ない")
    ck(not shown(pg, "#flrAdd") and not shown(pg, "#flrDel"), "階の +/- は出ない")

    # 図面を触っても書き戻さない
    n0 = pg.evaluate("window.__kv.size")
    before = pg.evaluate("[...window.__kv.values()][0]")
    pg.click("#m1")
    pg.mouse.click(600, 450)
    pg.wait_for_timeout(2600)          # AUTOSAVE_IDLE_MS = 1500 より長く待つ
    after = pg.evaluate("[...window.__kv.values()][0]")
    ck(after == before, "編集しても保存しない")
    ck(pg.evaluate("window.__kv.size") == n0, "キーも増えない")
    ck(errs == [], "JS エラーなし %s" % errs)
    pg.screenshot(path=os.path.join(HERE, "camplan_share.png"))
    ctx.close()

    print("== J. URL で指定した階が開く ==")
    SEED = KV % "true" + """
    window.__kv.set('A社/1F', '{"app":"camplan","version":1,"walls":[],"cameras":[]}');
    window.__kv.set('A社/2F', '{"app":"camplan","version":1,"walls":[],"cameras":[]}');
    window.__kv.set('B社/5F', '{"app":"camplan","version":1,"walls":[],"cameras":[]}');
    """
    ctx, pg, errs = open_page(
        b, [SEED, SHARE],
        URL + "?session=tok-x&building=" + "B%E7%A4%BE" + "&floor=5F")
    ck(pg.eval_on_selector("#bldSel", "e=>e.value") == "B社",
       "指定された建物: %s" % pg.eval_on_selector("#bldSel", "e=>e.value"))
    ck(pg.eval_on_selector_all("#flrList .row.on", "e=>e.map(x=>x.innerText)") == ["5F"],
       "指定された階が選ばれる")
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()

    print("== K. 指定した階が無ければ知らせて先頭を開く ==")
    ctx, pg, errs = open_page(
        b, [SEED, SHARE], URL + "?session=tok-x&building=C%E7%A4%BE&floor=9F")
    ck(pg.eval_on_selector("#bldSel", "e=>e.value") == "A社",
       "先頭に戻る: %s" % pg.eval_on_selector("#bldSel", "e=>e.value"))
    ck("見つかりませんでした" in pg.inner_text("#navMsg"),
       "知らせる: %s" % pg.inner_text("#navMsg"))
    ck(errs == [], "JS エラーなし %s" % errs)
    ctx.close()
    b.close()

print("")
print("%d failure(s)" % len(fails))
for f in fails:
    print("  -", f)
sys.exit(1 if fails else 0)

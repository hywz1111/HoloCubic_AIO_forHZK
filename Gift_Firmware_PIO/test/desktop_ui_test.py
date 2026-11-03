#!/usr/bin/env python3
"""Exercise the actual mobile page against a simulated ESP32 HTTP contract."""
import argparse
import copy
import json
from pathlib import Path
import shutil
from urllib.parse import parse_qs, urlparse
from playwright.sync_api import sync_playwright, expect


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", help="Optional system Chromium/Chrome executable")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "include/control_page.h"
    html = source.read_text().split('R"HTML(', 1)[1].split(')HTML";', 1)[0]
    state = {"sd": True, "paused": False, "uploading": False, "current": "01_stage.mjpeg", "error": "",
             "brightness": 55, "led": 8, "fps": 12, "rotation": 4, "movies": ["01_stage.mjpeg"],
             "desk": {"mode": 0, "view": 0, "off": False, "timeReady": False, "nightActive": False,
                      "night": 1, "start": 1380, "end": 480, "level": 8, "overlay": 0, "cards": 1,
                      "timerRunning": False, "timerFinished": False, "seconds": 0, "note": "",
                      "events": [{"name": "", "date": "2026-01-01", "annual": 0, "enabled": 0} for _ in range(3)]}}
    requests = []
    errors = []

    def handler(route):
        path = urlparse(route.request.url).path
        if path == "/":
            route.fulfill(status=200, content_type="text/html; charset=utf-8", body=html)
            return
        if path == "/api/state":
            route.fulfill(status=200, content_type="application/json", body=json.dumps(copy.deepcopy(state)))
            return
        if not path.startswith("/api/"):
            route.fulfill(status=404, body="")
            return
        assert route.request.method == "POST"
        assert route.request.headers.get("x-hazuki-request") == "1"
        data = {k: v[0] for k, v in parse_qs(route.request.post_data or "", keep_blank_values=True).items()}
        requests.append((path, data))
        desk = state["desk"]
        if path == "/api/time":
            assert 1704067200 <= int(data["epoch"]) <= 4102444799
            desk["timeReady"] = True
        elif path == "/api/view":
            desk["mode"] = desk["view"] = int(data["mode"])
        elif path == "/api/power":
            desk["off"] = bool(int(data["off"]))
        elif path == "/api/timer":
            desk["timerRunning"] = data["action"] == "start"
            desk["timerFinished"] = False
            desk["seconds"] = int(data.get("minutes", "0")) * 60
            desk["view"] = 4 if desk["timerRunning"] else desk["mode"]
        elif path == "/api/note":
            assert len(data["bitmap"]) == 192 * 72 // 8 * 2
            assert any(c != "0" for c in data["bitmap"])
            desk["note"] = data["text"]
        elif path == "/api/events":
            assert len(data["bitmap0"]) == 160 * 24 // 8 * 2
            assert data["enabled0"] == "1"
            desk["cards"] = int(data["cards"])
        elif path == "/api/night":
            for key in ("night", "start", "end", "level", "overlay", "cards"):
                desk[key] = int(data[key])
        route.fulfill(status=200, content_type="application/json", body='{"ok":true}')

    with sync_playwright() as playwright:
        options = {"headless": True, "args": ["--no-sandbox"]}
        executable = args.browser or shutil.which("chromium") or shutil.which("google-chrome")
        if executable:
            options["executable_path"] = executable
        browser = playwright.chromium.launch(**options)
        page = browser.new_page(viewport={"width": 390, "height": 844})
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.route("http://hazuki.test/**", handler)
        page.goto("http://hazuki.test/")
        expect(page.locator("#clockState")).to_contain_text("已校时", timeout=10000)
        assert any(p == "/api/time" for p, _ in requests)
        page.get_by_role("button", name="看时间", exact=True).click()
        expect(page.locator("#state")).to_have_text("桌面时钟")
        page.locator("#power").click()
        expect(page.locator("#state")).to_have_text("已熄屏")
        page.locator("#power").click()
        expect(page.locator("#state")).to_have_text("桌面时钟")
        page.get_by_role("button", name="5 分钟", exact=True).click()
        expect(page.locator("#timerState")).to_contain_text("05:00")
        page.locator("#stopTimer").click()
        expect(page.locator("#timerState")).to_have_text("计时未开始")
        text = '今天也要发光\n晗歌 はづき'
        page.locator("#note").fill(text)
        page.get_by_role("button", name="保存留言", exact=True).click()
        expect(page.locator("#notice")).to_have_text("留言已保存")
        assert next(data for path, data in requests if path == "/api/note")["text"] == text
        page.locator("#showNote").click()
        expect(page.locator("#state")).to_have_text("今日留言")
        page.locator("summary").filter(has_text="生日与重要日子").click()
        page.locator("#eventName0").fill("晗歌生日")
        page.locator("#eventDate0").fill("2027-02-28")
        page.locator("#eventAnnual0").check()
        page.get_by_role("button", name="保存纪念日", exact=True).click()
        expect(page.locator("#notice")).to_have_text("纪念日已保存")
        page.locator("#nightStart").fill("22:30")
        page.locator("#nightEnd").fill("07:15")
        page.locator("#nightLevel").fill("0")
        page.locator("#overlay").check()
        page.get_by_role("button", name="保存夜间设置", exact=True).click()
        expect(page.locator("#notice")).to_have_text("夜间设置已保存")
        night = next(data for path, data in requests if path == "/api/night")
        assert int(night["start"]) == 1350 and int(night["end"]) == 435 and night["level"] == "0"
        assert not errors, errors
        browser.close()
    print("mobile page checks passed: auto sync, views, sleep/wake, timer, Chinese/Japanese raster text, events, night settings")


if __name__ == "__main__":
    main()

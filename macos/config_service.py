#!/usr/bin/env python3
"""JSON-only local settings adapter for the WKWebView controller."""
import json
import sys
from settings import load, save, timezones

def main():
    try:
        request = json.loads(sys.stdin.read(262145))
        if request.get("action") == "load":
            data = {"values": load(), "timezones": timezones()}
        elif request.get("action") == "save":
            data = {"values": save(request.get("values"))}
        else:
            raise ValueError("无法识别的配置操作。")
        result = {"ok": True, "data": data}
    except Exception as error:
        result = {"ok": False, "error": str(error)}
    print(json.dumps(result, ensure_ascii=False))

if __name__ == "__main__":
    main()

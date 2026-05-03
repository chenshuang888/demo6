"""weather plugin —— 动态 app 调 ble.send('req') 取天气数据。

通用服务（bind_app=None），任何动态 app 都可以发 from=weather/type=req 来请求。

注意：本插件可以引用 companion.shared.geoip_weather，因为 shared/ 里的 geoip_weather
本质是"封装好的免费 API 客户端"，不是 companion 内部细节。但为了示范"插件应自带依赖"
的原则，未来这个 helper 可以从 shared/ 移到本目录下。当前阶段不做。
"""

from __future__ import annotations

import time

from companion.plugin_sdk import Plugin
from companion.shared.geoip_weather import get_weather


_WMO = {
    0: "clear", 1: "cloudy", 2: "cloudy", 3: "overcast",
    45: "fog", 48: "fog",
    51: "rain", 53: "rain", 55: "rain", 56: "rain", 57: "rain",
    61: "rain", 63: "rain", 65: "rain", 66: "rain", 67: "rain",
    71: "snow", 73: "snow", 75: "snow", 77: "snow", 85: "snow", 86: "snow",
    80: "rain", 81: "rain", 82: "rain",
    95: "thunder", 96: "thunder", 99: "thunder",
}


class WeatherPlugin(Plugin):
    plugin_id = "weather"
    title     = ""           # 无 GUI 页
    bind_app  = None          # 通用服务

    async def on_message(self, msg: dict) -> None:
        # 只关心 from=weather/type=req
        if msg.get("from") != "weather" or msg.get("type") != "req":
            return
        body = msg.get("body") if isinstance(msg.get("body"), dict) else {}
        force = bool(body.get("force"))
        try:
            snap = await get_weather(force=force)
            self.tx_to("weather", "data", body={
                "temp_c":   round(snap.temp_c, 1),
                "temp_min": round(snap.temp_min, 1),
                "temp_max": round(snap.temp_max, 1),
                "humidity": int(snap.humidity),
                "code":     _WMO.get(snap.wmo, "unknown"),
                "city":     snap.city,
                "desc":     snap.desc(),
                "ts":       int(time.time()),
            })
        except Exception as e:
            self.log.warning("weather fetch: %s", e)
            self.tx_to("weather", "error", body={"msg": str(e)})

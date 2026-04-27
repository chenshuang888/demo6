"""weather_provider —— 给 weather.js 提供数据。

数据源: OpenMeteo (https://open-meteo.com/) - 免费、无 key、HTTP GET。

协议:
  PC ←─ ESP    {to: "weather", type: "req"}                  // app 启动 / 用户刷新
  PC ─→ ESP    {to: "weather", type: "data", body: {...}}    // 一次完整快照
  PC ─→ ESP    {to: "weather", type: "error", body: {msg}}   // 拉取失败

body.data 字段:
  temp_c    float    当前温度
  temp_min  float    今日最低
  temp_max  float    今日最高
  humidity  int      湿度 %
  code      string   "clear"|"cloudy"|"overcast"|"rain"|"snow"|"fog"|"thunder"|"unknown"
  city      string   城市名
  desc      string   人类可读描述
  ts        int      生成时刻 unix sec

开发者要改的：
  - LATITUDE/LONGITUDE/CITY 改成你想要的城市坐标 + 显示名
  - 没了，其它不用动
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

import requests

from dynapp_sdk import DynappClient

logger = logging.getLogger(__name__)

# ----------------------------------------------------------------------------
# 配置（开发者按需修改）
# ----------------------------------------------------------------------------

# 默认上海。改成你想要的城市坐标。
LATITUDE  = 31.2304
LONGITUDE = 121.4737
CITY      = "Shanghai"

# 缓存 10 分钟，防止 ESP 反复进 weather app 把 API 打爆
CACHE_TTL_SEC = 600

# 拉取超时
HTTP_TIMEOUT_SEC = 10

# ----------------------------------------------------------------------------
# WMO 编码 → 我们的 code 字符串
# ----------------------------------------------------------------------------

def _wmo_to_code(wmo: int) -> str:
    if wmo == 0:                                       return "clear"
    if wmo in (1, 2):                                  return "cloudy"
    if wmo == 3:                                       return "overcast"
    if wmo in (45, 48):                                return "fog"
    if wmo in (51, 53, 55, 56, 57, 61, 63, 65,
               66, 67, 80, 81, 82):                    return "rain"
    if wmo in (71, 73, 75, 77, 85, 86):                return "snow"
    if wmo in (95, 96, 99):                            return "thunder"
    return "unknown"


_WMO_DESC = {
    0: "Clear",
    1: "Mainly Clear", 2: "Partly Cloudy", 3: "Overcast",
    45: "Fog", 48: "Fog",
    51: "Light Drizzle", 53: "Drizzle", 55: "Heavy Drizzle",
    56: "Freezing Drizzle", 57: "Freezing Drizzle",
    61: "Light Rain", 63: "Rain", 65: "Heavy Rain",
    66: "Freezing Rain", 67: "Freezing Rain",
    71: "Light Snow", 73: "Snow", 75: "Heavy Snow",
    77: "Snow Grains",
    80: "Light Showers", 81: "Showers", 82: "Heavy Showers",
    85: "Snow Showers", 86: "Heavy Snow Showers",
    95: "Thunderstorm", 96: "Thunder w/ Hail", 99: "Thunder w/ Hail",
}


# ----------------------------------------------------------------------------
# 拉取 + 缓存
# ----------------------------------------------------------------------------

_cache: dict[str, Any] | None = None
_cache_ts: float = 0.0


def _fetch_once() -> dict[str, Any]:
    url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={LATITUDE}&longitude={LONGITUDE}"
        "&current=temperature_2m,relative_humidity_2m,weather_code"
        "&daily=temperature_2m_max,temperature_2m_min"
        "&timezone=auto&forecast_days=1"
    )
    r = requests.get(url, timeout=HTTP_TIMEOUT_SEC)
    r.raise_for_status()
    j = r.json()
    cur = j["current"]
    daily = j["daily"]
    wmo = int(cur["weather_code"])
    return {
        "temp_c":   round(float(cur["temperature_2m"]), 1),
        "temp_min": round(float(daily["temperature_2m_min"][0]), 1),
        "temp_max": round(float(daily["temperature_2m_max"][0]), 1),
        "humidity": int(cur["relative_humidity_2m"]),
        "code":     _wmo_to_code(wmo),
        "city":     CITY,
        "desc":     _WMO_DESC.get(wmo, "Unknown"),
        "ts":       int(time.time()),
    }


async def _fetch_cached(force: bool = False) -> dict[str, Any]:
    """带 10 分钟缓存的拉取。force=True 跳过缓存。

    requests.get 是阻塞的，用 to_thread 避开事件循环阻塞。
    """
    global _cache, _cache_ts
    now = time.time()
    if (not force) and _cache is not None and (now - _cache_ts) < CACHE_TTL_SEC:
        logger.debug("weather: cache hit (age=%.1fs)", now - _cache_ts)
        return _cache
    data = await asyncio.to_thread(_fetch_once)
    _cache = data
    _cache_ts = now
    logger.info("weather: fetched %s", data)
    return data


# ----------------------------------------------------------------------------
# 注册到 client
# ----------------------------------------------------------------------------

def register_weather(client: DynappClient) -> None:
    """把 weather provider 接到 client 上。"""

    @client.on("weather", "req")
    async def _on_req(msg: dict) -> None:
        force = bool(msg.get("body", {}).get("force") if isinstance(msg.get("body"), dict) else False)
        try:
            data = await _fetch_cached(force=force)
            await client.send("weather", "data", body=data)
        except Exception as e:
            logger.error("weather fetch failed: %s", e)
            await client.send("weather", "error", body={"msg": str(e)})

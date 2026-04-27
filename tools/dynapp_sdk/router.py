"""router —— 把 ESP 推来的 JSON 消息按 (from_app, type) 分发给 handler。

设计:
  - register(from_app: str|None, type: str|None, handler) → handler key
      from_app=None 表示通配，type=None 也表示通配
  - dispatch(msg: dict) 优先级:
        具体 (from_app, type)  >  (from_app, *)  >  (*, type)  >  (*, *)
    每条消息只触发"最具体"的一个 handler；如果想监听所有消息用 register(None, None, ...)
  - 同步 / 异步 handler 都支持
  - handler 抛异常 → ERROR 级日志 + traceback，不向上传播
"""

from __future__ import annotations

import asyncio
import inspect
import logging
import traceback
from typing import Any, Callable, Optional

logger = logging.getLogger(__name__)

Handler = Callable[[dict], Any]   # 同步返回 Any / 异步返回 Awaitable


class Router:
    def __init__(self) -> None:
        # 四个桶按优先级降序排列
        self._exact: dict[tuple[str, str], Handler] = {}
        self._by_app: dict[str, Handler] = {}
        self._by_type: dict[str, Handler] = {}
        self._catch_all: Optional[Handler] = None

    # -------- 注册 --------

    def register(
        self,
        from_app: Optional[str],
        msg_type: Optional[str],
        handler: Handler,
    ) -> None:
        if from_app and msg_type:
            self._exact[(from_app, msg_type)] = handler
        elif from_app:
            self._by_app[from_app] = handler
        elif msg_type:
            self._by_type[msg_type] = handler
        else:
            self._catch_all = handler

    # -------- 分发 --------

    async def dispatch(self, msg: dict) -> None:
        from_app = msg.get("from")
        msg_type = msg.get("type")

        handler: Optional[Handler] = None
        if from_app and msg_type and (from_app, msg_type) in self._exact:
            handler = self._exact[(from_app, msg_type)]
        elif from_app and from_app in self._by_app:
            handler = self._by_app[from_app]
        elif msg_type and msg_type in self._by_type:
            handler = self._by_type[msg_type]
        elif self._catch_all is not None:
            handler = self._catch_all

        if handler is None:
            logger.debug("no handler for msg: from=%s type=%s", from_app, msg_type)
            return

        try:
            ret = handler(msg)
            if inspect.isawaitable(ret):
                await ret
        except Exception:
            logger.error(
                "handler raised on msg from=%s type=%s:\n%s",
                from_app, msg_type, traceback.format_exc(),
            )

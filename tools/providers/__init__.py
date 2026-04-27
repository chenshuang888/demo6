"""providers —— 每个动态 app 的 PC 配套服务集合。

每个 provider 是一个 register_xxx(client) 函数：把所有该 app 的回调
注册到 DynappClient 上、起好后台 task。

新增 app 步骤:
    1) 在本目录新建 my_provider.py
    2) 实现 def register_my(client: DynappClient): ...
    3) 在 ../dynapp_companion.py 里 from .providers.my_provider import register_my
       并在 main() 里调用 register_my(client)
"""

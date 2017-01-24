Lua CPU Profile
======
对指定的Lua函数进行profile，输出该函数调用的各子函数的运行占用时间。


## 实现原理
利用lua的lua_sethook API来监控函数调用
由于luajit下，递归尾调用暂时没办法判断，所以统计出来的totaltime不准确


## 用法:
见test.lua



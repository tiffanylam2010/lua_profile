Lua CPU Profile
======
对指定的Lua函数进行profile，输出该函数调用的各子函数的运行占用时间。


## 实现原理
利用lua的lua_sethook API来监控函数调用，把函数调用开始和结束的相关数据记录到共享内存中。
运行结束后，再通过python脚本（parse.py）分析共享内存中的数据统计出函数调用的时间和调用关系。


## 编译
* 修改makeme.sh中的lua路径，运行makeme.sh；生成cpu_profile.so和storage.so
  cpu_profile.so 供lua模块使用
  storage.so 供parse.py使用，用于分析运行结果

## 用法:
见test.lua


可运行sh runtest.sh 查看效果

```lua
local profile = require "cpu_profile"

local function  bar()
    local k = 0
    for i=1, 10000 do
        k = i*i
        for j=1, 1000 do
            local v = j*k
        end
    end
    return
end
local function foo()
    print("foo")
    return bar()
end


local function test(n)
	if n == 0 then
		return
	end
	for i=1,n do
	end
    foo()
    bar()
    bar()
	return test(n-1)
end

profile.init() -- 需要进行一次初始化操作
profile.profile(test, 3) -- 可多次profile一个函数
profile.profile(test, 5)

--profile.dump_stats()

-- 执行完毕后，profile的调用数据栈放在共享内存中
-- 需要运行python parse.py 来分析输出结果
```

* 运行结速后，运行 python parse.py 分析结果

```
total profile_time: 3.365 sec

     count  self_time %self_time total_time function
        24      3.364     99.98%      3.364 test.lua:3:bar
         8      0.000      0.01%      0.000 [C]:-1:print
         8      0.000      0.01%      1.150 test.lua:13:foo
        10      0.000      0.00%      8.778 test.lua:19:NULL
         8      0.000      0.00%      0.000 [C]:-1:NULL
```
输出结果：
* 第一行表示需要profile的函数一共运行了多长时间
* count：函数调用的次数
* self_time: 函数执行的时间（不包括子函数调用）
* %self_time: self_time占所有函数self_time总和的百分百比
* total_time: 函数调用包括子函数调用的总时间，有时候会比第一行的时间还长，因为包含了递归调用的时间。
* function: 文件名:行号:函数名

## TODO
(有空再补充)

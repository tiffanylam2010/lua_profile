local profile = require "time_profile"

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
profile.stats()


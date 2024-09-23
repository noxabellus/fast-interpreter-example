function fib(n)
    if n < 2 then
        return n
    else
        return fib(n - 1) + fib(n - 2)
    end
end

local start = os.clock()
local result = fib(32)
local stop = os.clock()

print (result, "(in ", stop - start, "s)")
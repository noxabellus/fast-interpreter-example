function ackermann(m, n)
    if m == 0.0 then return n + 1.0 end
    if n == 0.0 then return ackermann(m - 1.0, 1.0) end
    return ackermann(m - 1.0, ackermann(m, n - 1.0))
end

local loop_count = 10.0;
function loop_ackermann(m, n)
    local result = 0.0
    local i = 0.0
    while i ~= loop_count do
        result = result + ackermann(m, n)
        i = i + 1.0
    end
    return result
end

local start = os.clock()
local result = loop_ackermann(3.0, 8.0)
local stop = os.clock()

print(result, "(in ", stop - start, "s)")
local dir=arg[0]:match('^(.*)/') or '.'
local json=require('luci.jsonc')
local policy=dofile(dir..'/policy.lua')
local function read(path)
    local f=assert(io.open(path));local s=f:read('*a');f:close();return s
end
local c=assert(json.parse(read(arg[2])))
if arg[1]=='check' then policy.validate(c);return end
assert(arg[1]=='rules')
local pool={}
local f=io.open(arg[3]..'/pool')
if f then for a in f:lines() do if a~='' then pool[#pool+1]=a end end;f:close() end
if arg[9]=='fallback' then pool={} end
io.write(policy.generate(c,{family=tonumber(arg[4]),pool=pool,dev=arg[5],lan=arg[6],wan=arg[7],landev=arg[8],primary=arg[10]}))

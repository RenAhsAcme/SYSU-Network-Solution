local json=require('luci.jsonc')
local dir=arg[0]:match('^(.*)/') or '.'
local v6=dofile(dir..'/netcalc6.lua')
local family,iface,chosen=tonumber(arg[1]),arg[2],arg[3] or ''
assert((family==4 or family==6) and iface:match('^[%w_]+$'))
local p=assert(io.popen('ubus call network.interface.'..iface..' status 2>/dev/null'))
local data=json.parse(p:read('*a'));p:close()
assert(data and data.up, 'Interface unavailable')
local function normalize(s) return family==4 and s or v6.format(v6.parse(s)) end
local result
for _,a in ipairs(data['ipv'..family..'-address'] or {}) do
    local valid=family==4 or ((a.preferred or 0)>0 and a.address:match('^[23]'))
    if valid and (chosen=='' or normalize(chosen)==normalize(a.address)) then result=a;break end
end
assert(result,'Native address unavailable')
print(result.address..' '..result.mask)

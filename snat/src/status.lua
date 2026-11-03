local json=require('luci.jsonc')
local dir=arg[0]:match('^(.*)/') or '.'
local v6=dofile(dir..'/netcalc6.lua')
local function read(path,limit)
    local f=io.open(path);if not f then return '' end
    local s=f:read(limit or 65536) or '';f:close();return s
end
local function command(s)
    local p=io.popen(s);if not p then return '' end
    local data=p:read('*a');p:close();return data
end
local function decode(s) return json.parse(s or '') or {} end
local function norm(a,f)
    if f==4 then return a end
    local ok,p=pcall(v6.parse,a);return ok and v6.format(p) or a
end
local function exists(path) local f=io.open(path);if f then f:close();return true end;return false end
local function same(a,b)
    if type(a)~=type(b) then return false end
    if type(a)~='table' then return a==b end
    for k,v in pairs(a) do if not same(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end
    return true
end
local saved=decode(command('/usr/libexec/sysu-snat/config show 2>/dev/null'))
local applied=decode(read('/var/run/sysu-snat/config/policy.json'))
local service=decode(command("ubus call service list '{\"name\":\"sysu-snat\"}'"))
local instances=(service['sysu-snat'] or {}).instances or {}
local live=(instances.ipv4 or {}).running or (instances.ipv6 or {}).running
local c=live and applied.main and applied or saved
if not c.main then io.write(json.stringify({error='配置无效，请检查设置',families={}}));return end
local result={enabled=c.main.enabled==1,now=os.time(),families={},pending=live and not same(saved,applied) or false,
    dns_error=read('/var/run/sysu-snat/dns-error',512):match('^[^\n]*'),
    error=not saved.main and '已保存配置无效，当前保留此前运行配置' or nil}
for _,family in ipairs({4,6}) do
    local key='ipv'..family
    local f=c[key]
    local state='/var/run/sysu-snat/'..key
    local net=decode(command('ubus call network.interface.'..f.wan..' status 2>/dev/null'))
    local dev=net.l3_device or ''
    local native,pool,other={},{},{}
    local native_set,owned,bound={},{},{}
    for _,a in ipairs(net[key..'-address'] or {}) do
        native_set[norm(a.address,family)]=true
        native[#native+1]={address=a.address,prefix=a.mask,preferred=a.preferred,valid=a.valid,
            source=net.proto=='dhcp' and 'DHCP' or net.proto=='dhcpv6' and '自动配置（DHCPv6 / RA）' or net.proto or '系统配置'}
    end
    for _,a in ipairs(native) do
        if (family==4 or ((a.preferred or 0)>0 and a.address:match('^[23]'))) and (f.primary=='' or norm(a.address,family)==norm(f.primary,family)) then
            a.primary=true;break
        end
    end
    for line in read(state..'/owned'):gmatch('[^\n]+') do local _,a=line:match('^(%S+)%s+(%S+)$');if a then owned[norm(a,family)]=true end end
    if dev:match('^[%w_.:%-]+$') and #dev<=15 then
        local addresses=decode(command('ip -j -'..family..' addr show dev '..dev..' 2>/dev/null'))
        for _,iface in ipairs(addresses) do for _,a in ipairs(iface.addr_info or {}) do
            local address=norm(a['local'] or '',family)
            local usable=true
            for _,flag in ipairs(a.flags or {}) do if flag=='tentative' or flag=='dadfailed' then usable=false end end
            if a.tentative or a.dadfailed then usable=false end
            bound[address]=usable
            if not native_set[address] and not owned[address] then other[#other+1]={address=a['local'],prefix=a.prefixlen,scope=a.scope} end
        end end
    end
    local active=exists(state..'/active')
    local count,usable=0,0
    for a in read(state..'/pool'):gmatch('[^\n]+') do
        a=norm(a,family);count=count+1
        local ready=active and owned[a] and bound[a]==true
        if ready then usable=usable+1 end
        pool[#pool+1]={address=a,bound=bound[a]==true,usable=ready==true}
    end
    local running=(instances[key] or {}).running==true
    local nft=command('nft list table '..(family==4 and 'ip awp_auto' or 'ip6 awp6_auto')..' 2>/dev/null')
    local has_rules=#nft>0
    local status='stopped'
    if running then
        if exists(state..'/building') then status='building'
        elseif active and usable==count and count>0 and has_rules then status='active'
        elseif exists(state..'/fallback') and has_rules then status='fallback'
        elseif exists(state..'/unavailable') then status='waiting'
        elseif exists(state..'/retry-at') then status='retrying'
        else status='starting' end
    end
    result.families[key]={state=status,running=running,enabled=f.enabled==1,device=dev,interface=f.wan,
        native=native,pool=pool,other=other,count=count,usable=usable,rules=has_rules,
        phase=read(state..'/phase',256):gsub('\n',''),verified=tonumber(read(state..'/verified',32)),
        retry_at=tonumber(read(state..'/retry-at',32)),fallback=f.fallback,
        detail=(status=='waiting' or status=='retrying' or status=='fallback') and read(state..'/last-output',2048) or ''}
end
io.write(json.stringify(result)..'\n')

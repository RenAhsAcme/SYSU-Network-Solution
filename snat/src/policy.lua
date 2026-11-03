local dir = (arg and arg[0] or ''):match('^(.*)/') or '.'
local cli=arg
arg=nil
local v4 = dofile(dir..'/netcalc.lua')
local v6 = dofile(dir..'/netcalc6.lua')
arg=cli
local M = {}
function M.cidr(s, family)
    assert(type(s)=='string' and #s<=64, 'Invalid address')
    local a,p = s:match('^([^/]+)/(%d+)$')
    a,p = a or s, tonumber(p) or (family==4 and 32 or 128)
    if family==4 then local low,_,prefix=v4.network(a,p); return v4.ip(low)..'/'..prefix end
    return v6.network(a,p)
end
function M.port(s)
    local a,b=s:match('^(%d+)%-(%d+)$')
    a,b=tonumber(a or s),tonumber(b or a or s)
    assert(a and b and a>=1 and b<=65535 and a<=b, 'Invalid port range')
    return a==b and tostring(a) or a..'-'..b
end
function M.validate(c)
    M.cidr(c.main.tailscale_ipv4,4); M.cidr(c.main.tailscale_ipv6,6)
    for _,f in ipairs({4,6}) do
        local p=c['ipv'..f].primary
        if p~='' then M.cidr(p,f) end
    end
    for _,r in ipairs(c.rules) do
        local f=tonumber(r.family)
        for _,k in ipairs({'src','dest'}) do if r[k]~='' then M.cidr(r[k],f) end end
        for _,k in ipairs({'sport','dport'}) do if r[k]~='' then M.port(r[k]) end end
    end
end
function M.generate(c, ctx)
    M.validate(c)
    local family=ctx.family
    local f, main = c['ipv'..family],c.main
    local ip=family==4 and 'ip' or 'ip6'
    local name=family==4 and 'awp_auto' or 'awp6_auto'
    for _,dev in ipairs({ctx.dev,ctx.landev,main.tailscale_device}) do
        assert(dev:match('^[%w_.:%-]+$') and #dev<=15, 'Invalid device')
    end
    local primary=M.cidr(ctx.primary,family):match('^[^/]+')
    local entries={}
    for i,a in ipairs(ctx.pool) do
        a=M.cidr(a,family):match('^[^/]+')
        entries[i]=(i-1)..' : '..a
    end
    local n=#entries
    local out={'add table '..ip..' '..name, 'delete table '..ip..' '..name, 'table '..ip..' '..name..' {'}
    if n>0 then out[#out+1]=' map sources { typeof numgen random mod '..n..' : '..ip..' saddr; elements = { '..table.concat(entries,', ')..' }; }' end
    out[#out+1]=' chain postrouting { type nat hook postrouting priority 99; policy accept;'
    local function action(mode)
        if n==0 and mode~='original' and mode~='primary' then mode=f.fallback end
        if mode=='original' then return 'counter return' end
        if mode=='primary' then return 'counter snat to '..primary end
        if mode=='random' then return 'counter snat to numgen random mod '..n..' map @sources' end
        if mode=='host' then return 'counter snat to jhash '..ip..' saddr mod '..n..' seed 0x73797375 map @sources' end
        assert(mode=='endpoint', 'Invalid mode')
        return 'counter snat to jhash '..ip..' saddr . udp sport mod '..n..' seed 0x7461696c map @sources'
    end
    local function base(scope)
        local common='oifname "'..ctx.dev..'" '..ip..' daddr != '..M.cidr(ctx.wan,family)
        if scope=='proxy' then
            if main.proxy_enabled~=1 then return end
            assert(tostring(main.proxy_gid):match('^%d+$') and tonumber(main.proxy_gid)>0 and tonumber(main.proxy_gid)<4294967295)
            return 'meta skgid '..main.proxy_gid..' '..common
        end
        if scope=='tailscale' then
            if main.tailscale_enabled~=1 then return end
            return 'iifname "'..main.tailscale_device..'" '..ip..' saddr '..M.cidr(main['tailscale_ipv'..family],family)..' '..common
        end
        assert(scope=='lan')
        local src=M.cidr(ctx.lan,family)
        if family==6 then src='{ '..src..', '..M.cidr(ctx.wan,family)..' }' end
        return 'iifname "'..ctx.landev..'" '..ip..' saddr '..src..' '..common
    end
    -- 有序例外先匹配，随后才使用各流量来源的默认策略。
    for _,r in ipairs(c.rules) do
        if tonumber(r.family)==family then
            local b=base(r.scope)
            if b then
                for _,k in ipairs({'src','dest'}) do if r[k]~='' then b=b..' '..ip..(k=='src' and ' saddr ' or ' daddr ')..M.cidr(r[k],family) end end
                if r.proto~='any' then b=b..' meta l4proto '..r.proto end
                for _,k in ipairs({'sport','dport'}) do if r[k]~='' then b=b..' '..r.proto..' '..k..' '..M.port(r[k]) end end
                out[#out+1]=' '..b..' '..action(r.mode)
            end
        end
    end
    for _,scope in ipairs({'lan','tailscale','proxy'}) do
        local b=base(scope)
        if b then
            for _,proto in ipairs({'tcp','udp','other'}) do
                out[#out+1]=' '..b..(proto=='other' and '' or ' meta l4proto '..proto)..' '..action(f[scope..'_'..proto])
            end
        end
    end
    out[#out+1]=' }'; out[#out+1]='}'
    return table.concat(out,'\n')..'\n'
end
return M

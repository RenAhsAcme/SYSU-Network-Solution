local dir=arg[0]:match('^(.*)/') or '.'
arg[0]=dir..'/../src/policy.lua'
local policy=dofile(arg[0])
local c={main={proxy_enabled=1,proxy_gid='65534',tailscale_enabled=1,tailscale_device='tailscale0',
    tailscale_ipv4='100.64.0.0/10',tailscale_ipv6='fd7a:115c:a1e0::/48'},rules={}}
for _,family in ipairs({4,6}) do
    local f={primary='',fallback='primary'}
    for _,scope in ipairs({'lan','proxy','tailscale'}) do
        f[scope..'_tcp']='random';f[scope..'_udp']='endpoint';f[scope..'_other']='host'
    end
    c['ipv'..family]=f
end
local ctx={family=4,dev='eth0',landev='br-lan',primary='10.1.2.2',wan='10.1.2.0/29',lan='192.168.1.0/24',pool={'10.1.2.3','10.1.2.4'}}
local count=0
local function has(s,part) count=count+1;assert(s:find(part,1,true),part) end
local function lacks(s,part) count=count+1;assert(not s:find(part,1,true),part) end
local function rejects(fn) count=count+1;assert(not pcall(fn)) end
local rule={family='4',scope='lan',proto='udp',src='192.168.1.50',dest='203.0.113.0/24',sport='',dport='4000-4010',mode='primary'}
c.rules={rule}
local s=policy.generate(c,ctx)
has(s,'udp dport 4000-4010 counter snat to 10.1.2.2')
has(s,'ip saddr 192.168.1.50/32 ip daddr 203.0.113.0/24')
has(s,'jhash ip saddr . udp sport mod 2')
lacks(s,'jhash ip saddr . udp sport . ip daddr')
has(s,'meta skgid 65534')
has(s,'iifname "tailscale0" ip saddr 100.64.0.0/10')
has(s,'snat to numgen random mod 2')
has(s,'jhash ip saddr mod 2')
assert(s:find('udp dport',1,true)<s:find('meta l4proto tcp',1,true));count=count+1
rule.mode='original';has(policy.generate(c,ctx),'udp dport 4000-4010 counter return')
ctx.pool={};s=policy.generate(c,ctx)
lacks(s,'map sources');lacks(s,'mod 0');has(s,'snat to 10.1.2.2')
c.ipv4.fallback='original';lacks(policy.generate(c,ctx),'snat to')
ctx.pool={'10.1.2.3','10.1.2.4'}
c.main.proxy_enabled=0;c.main.tailscale_enabled=0
s=policy.generate(c,ctx);lacks(s,'skgid');lacks(s,'iifname "tailscale0"')
for _,bad in ipairs({'0','65536','12-1','1; delete table inet fw4'}) do rejects(function() policy.port(bad) end) end
for _,bad in ipairs({'1.2.3.999','1.2.3.4/33','1.2.3.4;flush ruleset'}) do rejects(function() policy.cidr(bad,4) end) end
rejects(function() policy.cidr('fd00::/129',6) end)
c.rules={};c.main.proxy_enabled=1;c.main.tailscale_enabled=1
ctx={family=6,dev='eth0',landev='br-lan',primary='2001:db8::1',wan='2001:db8::/64',lan='fd00::/60',pool={'2001:db8::2','2001:db8::3'}}
s=policy.generate(c,ctx)
has(s,'table ip6 awp6_auto');has(s,'jhash ip6 saddr . udp sport mod 2')
has(s,'ip6 saddr { fd00:0000:0000:0000:0000:0000:0000:0000/60, 2001:0db8:0000:0000:0000:0000:0000:0000/64 }')
c.ipv6.lan_tcp='primary';has(policy.generate(c,ctx),'snat to 2001:0db8:0000:0000:0000:0000:0000:0001')
if arg[1] then
    for _,f in ipairs({4,6}) do
        ctx.family=f
        if f==4 then ctx.primary='10.1.2.2';ctx.wan='10.1.2.0/29';ctx.lan='192.168.1.0/24';ctx.pool={'10.1.2.3','10.1.2.4'}
        else ctx.primary='2001:db8::1';ctx.wan='2001:db8::/64';ctx.lan='fd00::/60';ctx.pool={'2001:db8::2','2001:db8::3'} end
        local out=assert(io.open(arg[1]..'/policy'..f..'.nft','w'));out:write(policy.generate(c,ctx));out:close()
    end
end
print('policy: '..count..' assertions passed')

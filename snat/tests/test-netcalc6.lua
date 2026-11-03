local dir=arg[0]:match('^(.*)/') or '.'
local M=dofile(dir..'/../src/netcalc6.lua')
local n=0
local function eq(a,b) assert(a==b,tostring(a)..' != '..tostring(b));n=n+1 end
local function fails(f) assert(not pcall(f));n=n+1 end
eq(M.format(M.parse('2001:db8:1:2::1')),'2001:0db8:0001:0002:0000:0000:0000:0001')
eq(M.format(M.parse('::')),'0000:0000:0000:0000:0000:0000:0000:0000')
eq(M.network('fd12:3456:789a:7::1',60),'fd12:3456:789a:0000:0000:0000:0000:0000/60')
local w,l=M.plan('2001:db8:1:2::1',64,'fd12:3456:789a::',60)
eq(w,'2001:0db8:0001:0002:0000:0000:0000:0000/64')
eq(l,'fd12:3456:789a:0000:0000:0000:0000:0000/60')
for _,s in ipairs({'2001:::1','1:2:3:4:5:6:7','1:2:3:4:5:6:7:8:9','abcd:10000::1','::ffff:192.0.2.1','fe80::1%eth2','::;reboot'}) do fails(function() M.parse(s) end) end
fails(function() M.plan('fe80::1',64,'fddd::',60) end)
fails(function() M.plan('2001:db8::1',56,'fddd::',60) end)
fails(function() M.plan('2001:db8::1',64,'2001:db8:1::',64) end)
local serial=0
local random={read=function() serial=serial+1;return string.char(0,0,0,0,0,0,math.floor(serial/256),serial%256) end}
local ips=M.candidates('2001:db8:1:2::1',254,random)
eq(#ips,254)
eq(ips[1],'2001:0db8:0001:0002:0000:0000:0000:0002')
eq(ips[254],'2001:0db8:0001:0002:0000:0000:0000:00ff')
local seen={};for _,ip in ipairs(ips) do assert(not seen[ip]);seen[ip]=true;eq(M.network(ip,64),'2001:0db8:0001:0002:0000:0000:0000:0000/64') end
fails(function() M.candidates('2001:db8::1',513,random) end)
local r=M.rules('pool',{ips[1],ips[2]},'eth2',l,w,'br-lan')
assert(r:find('iifname "br-lan"',1,true) and r:find('numgen random mod 2 map @sources',1,true));n=n+1
assert(r:find('ip6 saddr { '..l..', '..w..' }',1,true));n=n+1
fails(function() M.rules('pool',{},'eth2',l,w,'br-lan') end)
fails(function() M.rules('pool',ips,'eth2;reboot',l,w,'br-lan') end)
print('netcalc6: '..n..' checks passed')

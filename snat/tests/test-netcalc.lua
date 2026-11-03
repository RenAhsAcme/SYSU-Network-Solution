local dir = arg[0]:match('^(.*)/') or '.'
local M = dofile(dir..'/../src/netcalc.lua')
local tests = 0
local function eq(a,b) assert(a==b, tostring(a)..' != '..tostring(b)); tests=tests+1 end
local function fails(f) assert(not pcall(f)); tests=tests+1 end
local p=M.plan('10.25.148.53',22,'10.25.148.1','192.168.1.1',24)
eq(p.wan,'10.25.148.0/22'); eq(p.pool,'10.25.151.0/24'); eq(#p.addresses,254)
eq(p.addresses[1],'10.25.151.1'); eq(p.addresses[254],'10.25.151.254')
p=M.plan('10.25.193.210',22,'10.25.192.1','192.168.10.1',24)
eq(p.pool,'10.25.195.0/24'); eq(#p.addresses,254)
eq(p.addresses[1],'10.25.195.1'); eq(p.addresses[254],'10.25.195.254')
p=M.plan('10.1.2.2',23,'10.1.2.1','192.168.1.1',24)
eq(p.pool,'10.1.3.0/24'); eq(#p.addresses,254)
p=M.plan('10.1.3.2',23,'10.1.3.1','192.168.1.1',24)
eq(p.pool,'10.1.3.0/24'); eq(#p.addresses,252); eq(p.addresses[1],'10.1.3.3')
p=M.plan('192.168.20.2',24,'192.168.20.1','192.168.1.1',24)
eq(#p.addresses,252); eq(p.addresses[1],'192.168.20.3')
p=M.plan('10.1.2.130',25,'10.1.2.129','192.168.1.1',24)
eq(p.pool,'10.1.2.128/25'); eq(#p.addresses,124); eq(p.addresses[1],'10.1.2.131')
p=M.plan('10.1.2.1',30,'10.1.2.2','192.168.1.1',24); eq(#p.addresses,0)
fails(function() M.number('300.1.1.1') end)
fails(function() M.number('1.2.3;reboot') end)
fails(function() M.plan('203.0.113.2',24,'203.0.113.1','192.168.1.1',24) end)
fails(function() M.plan('10.25.148.53',22,'10.25.148.1','10.25.149.1',24) end)
fails(function() M.plan('10.0.0.2',24,'10.0.1.1','192.168.1.1',24) end)
fails(function() M.plan('10.0.0.2',33,'10.0.0.1','192.168.1.1',24) end)
fails(function() M.plan('10.0.0.0',24,'10.0.0.1','192.168.1.1',24) end)
local r=M.rules({'10.0.0.3','10.0.0.4','10.0.0.5'},'eth0','192.168.1.0/24','10.0.0.0/24','AWP_AUTO')
assert(r:find('probability 0.33333333333',1,true)); tests=tests+1
assert(r:find('probability 0.50000000000',1,true)); tests=tests+1
assert(r:find('-A AWP_AUTO -j SNAT --to-source 10.0.0.5',1,true)); tests=tests+1
assert(r:find('! -s 192.168.1.0/24 -j RETURN',1,true)); tests=tests+1
fails(function() M.rules({},'eth0','192.168.1.0/24','10.0.0.0/24','AWP_AUTO') end)
fails(function() M.rules({'10.0.0.3'},'eth0;reboot','192.168.1.0/24','10.0.0.0/24','AWP_AUTO') end)
print('netcalc: '..tests..' checks passed')

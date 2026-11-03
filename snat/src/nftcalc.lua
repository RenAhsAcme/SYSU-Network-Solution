local dir = arg[0]:match('^(.*)/') or '.'
local M = dofile(dir..'/netcalc.lua')
local mode, file, dev, lan, wan = arg[1], arg[2], arg[3], arg[4], arg[5]
local proxy_gid = arg[6]
if proxy_gid and proxy_gid ~= '' then
  assert(proxy_gid:match('^%d+$') and tonumber(proxy_gid) > 0 and tonumber(proxy_gid) < 4294967295, 'Invalid proxy GID')
end
assert(mode == 'pool' or mode == 'probe', 'Invalid mode')
assert(dev:match('^[%w_.:%-]+$'), 'Invalid device')
local ips = {}
for line in io.lines(file) do
  if line ~= '' then M.number(line); ips[#ips+1] = line end
end
assert(#ips > 0, 'Empty pool')
local name = mode == 'pool' and 'awp_auto' or 'awp_probe'
local priority = mode == 'pool' and 99 or 98
local entries = {}
for i, ip in ipairs(ips) do
  entries[#entries+1] = (mode == 'pool' and tostring(i-1) or ip)..' : '..ip
end
print('add table ip '..name)
print('flush table ip '..name)
print('table ip '..name..' {')
local datatype = mode == 'pool' and ('typeof numgen random mod '..#ips..' : ip saddr') or 'type ipv4_addr : ipv4_addr'
print(' map sources { '..datatype..'; elements = { '..table.concat(entries, ', ')..' }; }')
print(' chain postrouting { type nat hook postrouting priority '..priority..'; policy accept;')
if mode == 'pool' then
  assert(lan:match('^[%d%.]+/%d+$') and wan:match('^[%d%.]+/%d+$'), 'Invalid subnet')
  print(' ip saddr '..lan..' oifname "'..dev..'" ip daddr != '..wan..' counter snat to numgen random mod '..#ips..' map @sources')
  -- TCP 按连接分配；UDP 固定源端点的出口，避免干扰打洞。
  local tail = ' iifname "tailscale0" ip saddr 100.64.0.0/10 oifname "'..dev..'" ip daddr != '..wan
  print(tail..' meta l4proto tcp counter snat to numgen random mod '..#ips..' map @sources')
  print(tail..' meta l4proto udp counter snat to jhash ip saddr . udp sport mod '..#ips..' seed 0x7461696c map @sources')
  print(tail..' counter snat to jhash ip saddr mod '..#ips..' seed 0x7461696c map @sources')
  if proxy_gid and proxy_gid ~= '' then
    print(' meta skgid '..proxy_gid..' oifname "'..dev..'" ip daddr != '..wan..' counter snat to numgen random mod '..#ips..' map @sources')
  end
else
  -- 同源 SNAT 锁定探测源地址，避免后续 masquerade 造成假阳性。
  print(' oifname "'..dev..'" counter snat to ip saddr map @sources')
end
print(' }')
print('}')

-- Lua 5.1+; no network or filesystem side effects.
local M = {}
function M.number(s)
  local a,b,c,d = tostring(s):match('^(%d+)%.(%d+)%.(%d+)%.(%d+)$')
  assert(a, 'Invalid IPv4 address')
  local parts = {tonumber(a), tonumber(b), tonumber(c), tonumber(d)}
  local n = 0
  for _, v in ipairs(parts) do assert(v <= 255, 'Invalid IPv4 octet'); n = n*256+v end
  return n
end
function M.ip(n)
  return string.format('%d.%d.%d.%d', math.floor(n/16777216)%256,
    math.floor(n/65536)%256, math.floor(n/256)%256, n%256)
end
function M.network(ip, prefix)
  prefix = tonumber(prefix)
  assert(prefix and prefix == math.floor(prefix) and prefix >= 0 and prefix <= 32, 'Invalid prefix')
  local size = 2^(32-prefix)
  local low = math.floor(M.number(ip)/size)*size
  return low, low+size-1, prefix
end
function M.private(n)
  return (n >= M.number('10.0.0.0') and n <= M.number('10.255.255.255'))
    or (n >= M.number('172.16.0.0') and n <= M.number('172.31.255.255'))
    or (n >= M.number('192.168.0.0') and n <= M.number('192.168.255.255'))
end
function M.plan(ip, prefix, gateway, lan_ip, lan_prefix)
  local low, high, p = M.network(ip, prefix)
  local lan_low, lan_high, lp = M.network(lan_ip, lan_prefix)
  local own, gw = M.number(ip), M.number(gateway)
  assert(M.private(low) and M.private(high), 'Automatic discovery is limited to RFC1918 WAN networks')
  assert(p >= 8 and p <= 30, 'WAN prefix must be /8 through /30')
  assert(own > low and own < high, 'WAN address is a network/broadcast address')
  assert(gw > low and gw < high, 'Gateway is outside usable WAN subnet')
  assert(high < lan_low or low > lan_high, 'WAN and LAN subnets overlap')
  local pool_low, pool_high, pp = low, high, p
  if p < 24 then pool_low = high-255; pp = 24 end
  local addresses = {}
  for n = pool_low+1, pool_high-1 do
    if n ~= own and n ~= gw then addresses[#addresses+1] = M.ip(n) end
  end
  return {wan=M.ip(low)..'/'..p, lan=M.ip(lan_low)..'/'..lp,
    pool=M.ip(pool_low)..'/'..pp, addresses=addresses}
end
function M.rules(addresses, dev, lan, wan, chain)
  assert(#addresses > 0, 'Empty source address pool')
  assert(dev:match('^[%w_.:%-]+$'), 'Invalid interface name')
  assert(chain:match('^[%w_]+$'), 'Invalid chain name')
  local out = {'*nat', ':'..chain..' - [0:0]', '-F '..chain,
    '-A '..chain..' ! -s '..lan..' -j RETURN',
    '-A '..chain..' ! -o '..dev..' -j RETURN',
    '-A '..chain..' -d '..wan..' -j RETURN'}
  for i, ip in ipairs(addresses) do
    M.number(ip)
    local remain = #addresses-i+1
    local rule = '-A '..chain
    if remain > 1 then rule = rule..string.format(' -m statistic --mode random --probability %.11f', 1/remain) end
    out[#out+1] = rule..' -j SNAT --to-source '..ip
  end
  out[#out+1] = 'COMMIT'
  return table.concat(out, '\n')..'\n'
end

if arg and arg[1] == 'plan' then
  local ok, p = pcall(M.plan, arg[2], arg[3], arg[4], arg[5], arg[6])
  if not ok then io.stderr:write(tostring(p)..'\n'); os.exit(1) end
  print(p.wan..' '..p.lan..' '..p.pool)
  for _, ip in ipairs(p.addresses) do print(ip) end
elseif arg and arg[1] == 'rules' then
  local ips = {}
  for line in io.lines(arg[2]) do if line ~= '' then ips[#ips+1] = line end end
  io.write(M.rules(ips, arg[3], arg[4], arg[5], arg[6]))
end
return M

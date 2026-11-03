local M = {}
function M.parse(s)
  assert(type(s) == 'string' and s:match('^[%x:]+$'), 'Invalid IPv6 address')
  local left, right = s:match('^(.-)::(.-)$')
  local function parts(v)
    local out = {}
    if v ~= '' then
      assert(not v:match('^:') and not v:match(':$') and not v:find('::'), 'Invalid IPv6 groups')
      for h in v:gmatch('[^:]+') do assert(#h <= 4); out[#out+1] = assert(tonumber(h,16)) end
    end
    return out
  end
  local out = parts(left or s)
  if left then
    local tail = parts(right)
    assert(#out+#tail < 8, 'Invalid compression')
    for _=1,8-#out-#tail do out[#out+1]=0 end
    for _,h in ipairs(tail) do out[#out+1]=h end
  end
  assert(#out == 8, 'Invalid group count')
  return out
end
function M.format(a)
  local out = {}
  for i=1,8 do out[i]=string.format('%04x',a[i]) end
  return table.concat(out,':')
end
function M.network(s,p)
  p=tonumber(p); assert(p and p==math.floor(p) and p>=0 and p<=128)
  local a=M.parse(s)
  for i=1,8 do
    local bits=math.max(0,math.min(16,p-(i-1)*16))
    local step=2^(16-bits); a[i]=math.floor(a[i]/step)*step
  end
  return M.format(a)..'/'..p
end
function M.plan(wan,mask,lan,lanmask)
  local w,l=M.parse(wan),M.parse(lan)
  assert(tonumber(mask)==64 and w[1]>=0x2000 and w[1]<0x4000, 'WAN needs a global /64')
  assert(l[1]>=0xfc00 and l[1]<=0xfdff and tonumber(lanmask)>=48 and tonumber(lanmask)<=64, 'LAN needs a ULA /48 through /64')
  return M.network(wan,64),M.network(lan,lanmask)
end
function M.candidates(wan,count,random)
  count=tonumber(count); assert(count and count==math.floor(count) and count>=1 and count<=512)
  local base=M.parse(wan); local own=M.format(base); local out,seen={},{}
  while #out<count do
    local s=assert(random:read(8)); assert(#s==8)
    local a={base[1],base[2],base[3],base[4]}
    for i=1,8,2 do a[#a+1]=s:byte(i)*256+s:byte(i+1) end
    -- 随机 IID 保留本地位，避免冒充硬件生成的接口标识。
    a[5]=a[5]-math.floor(a[5]/512)%2*512
    local ip=M.format(a)
    if ip~=own and not seen[ip] and a[5]+a[6]+a[7]+a[8]>0 then seen[ip]=true;out[#out+1]=ip end
  end
  return out
end
function M.rules(mode,ips,dev,lan,wan,landev,proxy_gid)
  assert(mode=='pool' or mode=='probe'); assert(#ips>0)
  assert(dev:match('^[%w_.:%-]+$'))
  if proxy_gid and proxy_gid~='' then
    assert(proxy_gid:match('^%d+$') and tonumber(proxy_gid)>0 and tonumber(proxy_gid)<4294967295, 'Invalid proxy GID')
  end
  local name=mode=='pool' and 'awp6_auto' or 'awp6_probe'
  local entries={}
  for i,ip in ipairs(ips) do
    ip=M.format(M.parse(ip))
    entries[i]=(mode=='pool' and tostring(i-1) or ip)..' : '..ip
  end
  local dtype=mode=='pool' and ('typeof numgen random mod '..#ips..' : ip6 saddr') or 'type ipv6_addr : ipv6_addr'
  local out={'add table ip6 '..name,'delete table ip6 '..name,'table ip6 '..name..' {',
    ' map sources { '..dtype..'; elements = { '..table.concat(entries,', ')..' }; }',
    ' chain postrouting { type nat hook postrouting priority '..(mode=='pool' and 99 or 98)..'; policy accept;'}
  if mode=='pool' then
    assert(landev:match('^[%w_.:%-]+$'))
    local la,lp=lan:match('^([^/]+)/(%d+)$');local wa,wp=wan:match('^([^/]+)/(%d+)$')
    assert(M.network(la,lp)==lan and M.network(wa,wp)==wan)
    out[#out+1]=' iifname "'..landev..'" ip6 saddr { '..lan..', '..wan..' } oifname "'..dev..'" ip6 daddr != '..wan..' counter snat to numgen random mod '..#ips..' map @sources'
    -- UDP 的出口选择不依赖目的地址，保留打洞所需的稳定性。
    local tail=' iifname "tailscale0" ip6 saddr fd7a:115c:a1e0::/48 oifname "'..dev..'" ip6 daddr != '..wan
    out[#out+1]=tail..' meta l4proto tcp counter snat to numgen random mod '..#ips..' map @sources'
    out[#out+1]=tail..' meta l4proto udp counter snat to jhash ip6 saddr . udp sport mod '..#ips..' seed 0x7461696c map @sources'
    out[#out+1]=tail..' counter snat to jhash ip6 saddr mod '..#ips..' seed 0x7461696c map @sources'
    if proxy_gid and proxy_gid~='' then
      out[#out+1]=' meta skgid '..proxy_gid..' oifname "'..dev..'" ip6 daddr != '..wan..' counter snat to numgen random mod '..#ips..' map @sources'
    end
  else out[#out+1]=' oifname "'..dev..'" counter snat to ip6 saddr map @sources' end
  out[#out+1]=' }';out[#out+1]='}'
  return table.concat(out,'\n')..'\n'
end
if arg and arg[1]=='plan' then
  local w,l=M.plan(arg[2],arg[3],arg[4],arg[5]);print(w..' '..l)
elseif arg and arg[1]=='generate' then
  local f=assert(io.open('/dev/urandom','rb'))
  for _,ip in ipairs(M.candidates(arg[2],arg[3],f)) do print(ip) end
  f:close()
elseif arg and arg[1]=='normalize' then
  for line in io.lines() do print(M.format(M.parse(line:match('^[^/]+')))) end
elseif arg and (arg[1]=='pool' or arg[1]=='probe') then
  local ips={};for ip in io.lines(arg[2]) do if ip~='' then ips[#ips+1]=ip end end
  io.write(M.rules(arg[1],ips,arg[3],arg[4],arg[5],arg[6],arg[7]))
end
return M

local path = "/boot/modules/hello.lua"
print("loadfile test:", path)
local f, err = loadfile(path)
if f then
  print("loadfile ok")
else
  print("loadfile error:", err)
end

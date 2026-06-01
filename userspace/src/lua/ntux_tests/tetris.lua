local ntux = require("ntux")

local win = ntux.window.create(0, 140, 90, 520, 620, "Lua Tetris")
ntux.window.show(win, 1)
ntux.window.focus(win)

local cell = 24
local grid_w = 10
local grid_h = 20
local ox = 40
local oy = 60

local shapes = {
  I = { {0,1, 1,1, 2,1, 3,1}, {2,0, 2,1, 2,2, 2,3} },
  O = { {1,0, 2,0, 1,1, 2,1} },
  T = { {1,0, 0,1, 1,1, 2,1}, {1,0, 1,1, 2,1, 1,2}, {0,1, 1,1, 2,1, 1,2}, {1,0, 0,1, 1,1, 1,2} },
  L = { {0,0, 0,1, 0,2, 1,2}, {0,1, 1,1, 2,1, 0,2}, {0,0, 1,0, 1,1, 1,2}, {2,0, 0,1, 1,1, 2,1} },
  J = { {1,0, 1,1, 1,2, 0,2}, {0,0, 0,1, 1,1, 2,1}, {0,0, 1,0, 0,1, 0,2}, {0,1, 1,1, 2,1, 2,2} },
  S = { {1,0, 2,0, 0,1, 1,1}, {1,0, 1,1, 2,1, 2,2} },
  Z = { {0,0, 1,0, 1,1, 2,1}, {2,0, 1,1, 2,1, 1,2} },
}

local colors = {0xFF2BC6FF, 0xFFFFD166, 0xFF7CFF6B, 0xFFFF7AA2, 0xFFB98CFF, 0xFFFF9F43, 0xFF4FD5FF}
local shape_keys = {"I","O","T","L","J","S","Z"}

local grid = {}
for y=1,grid_h do
  grid[y] = {}
  for x=1,grid_w do grid[y][x]=0 end
end

local function new_piece()
  local idx = (ntux.ticks() % #shape_keys) + 1
  local key = shape_keys[idx]
  return {k=key, r=1, x=4, y=0, c=colors[idx]}
end

local cur = new_piece()
local fall_acc = 0
local score = 0

local function cells(piece)
  local rot = shapes[piece.k][piece.r]
  local out = {}
  for i=1,#rot,2 do
    table.insert(out, {piece.x + rot[i], piece.y + rot[i+1]})
  end
  return out
end

local function can_place(piece, nx, ny, nr)
  local rot = shapes[piece.k][nr]
  for i=1,#rot,2 do
    local x = nx + rot[i]
    local y = ny + rot[i+1]
    if x < 0 or x >= grid_w or y >= grid_h then return false end
    if y >= 0 and grid[y+1][x+1] ~= 0 then return false end
  end
  return true
end

local function lock_piece(piece)
  for _,p in ipairs(cells(piece)) do
    if p[2] >= 0 then grid[p[2]+1][p[1]+1] = piece.c end
  end
  -- clear lines
  local cleared = 0
  for y=grid_h,1,-1 do
    local full = true
    for x=1,grid_w do if grid[y][x]==0 then full=false break end end
    if full then
      table.remove(grid, y)
      local row = {}
      for x=1,grid_w do row[x]=0 end
      table.insert(grid, 1, row)
      cleared = cleared + 1
      y = y + 1
    end
  end
  score = score + cleared * 100
end

local function draw()
  ntux.window.clear(win, 0xFF0B1118)
  ntux.window.text(win, 20, 20, 0xFFBFD7FF, "Lua Tetris  |  Arrows move  Space rotate")
  ntux.window.text(win, 380, 20, 0xFF9ED1FF, "Score: "..tostring(score))
  for y=0,grid_h-1 do
    for x=0,grid_w-1 do
      ntux.window.rect(win, ox + x*cell, oy + y*cell, cell-1, cell-1, 0xFF101A27, 0)
      local c = grid[y+1][x+1]
      if c ~= 0 then
        ntux.window.rect(win, ox + x*cell+1, oy + y*cell+1, cell-3, cell-3, c, 1)
      end
    end
  end
  for _,p in ipairs(cells(cur)) do
    if p[2] >= 0 then
      ntux.window.rect(win, ox + p[1]*cell+1, oy + p[2]*cell+1, cell-3, cell-3, cur.c, 1)
    end
  end
  ntux.window.present(win)
end

local function step_input()
  local left = ntux.input.key_edge(0x4B)
  local right = ntux.input.key_edge(0x4D)
  local down = ntux.input.key_edge(0x50)
  local rot = ntux.input.key_edge(0x39)
  if left and can_place(cur, cur.x-1, cur.y, cur.r) then cur.x = cur.x-1 end
  if right and can_place(cur, cur.x+1, cur.y, cur.r) then cur.x = cur.x+1 end
  if down and can_place(cur, cur.x, cur.y+1, cur.r) then cur.y = cur.y+1 end
  if rot then
    local nr = cur.r + 1
    if nr > #shapes[cur.k] then nr = 1 end
    if can_place(cur, cur.x, cur.y, nr) then cur.r = nr end
  end
end

local function step_fall()
  if can_place(cur, cur.x, cur.y+1, cur.r) then
    cur.y = cur.y + 1
  else
    lock_piece(cur)
    cur = new_piece()
    if not can_place(cur, cur.x, cur.y, cur.r) then
      score = 0
      for y=1,grid_h do for x=1,grid_w do grid[y][x]=0 end end
    end
  end
end

while not ntux.window.should_close(win) do
  step_input()
  fall_acc = fall_acc + 1
  if fall_acc >= 12 then
    fall_acc = 0
    step_fall()
  end
  draw()
  ntux.sleep_ms(16)
end

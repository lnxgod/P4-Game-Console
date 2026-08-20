-- SPDX-License-Identifier: MIT
-- Reference implementation of the host-provided p4.arcade v1 helpers.

local arcade = { version = 1 }

arcade.BLACK = 0x0000
arcade.BLUE = 0x0015
arcade.GREEN = 0x0540
arcade.CYAN = 0x0555
arcade.RED = 0xa800
arcade.MAGENTA = 0xa815
arcade.BROWN = 0xaa80
arcade.LIGHT_GRAY = 0xad55
arcade.DARK_GRAY = 0x52aa
arcade.LIGHT_BLUE = 0x52bf
arcade.LIGHT_GREEN = 0x57ea
arcade.LIGHT_CYAN = 0x57ff
arcade.LIGHT_RED = 0xfaaa
arcade.LIGHT_MAGENTA = 0xfabf
arcade.YELLOW = 0xffea
arcade.WHITE = 0xffff

local MAX_ACTORS = 64

local function integer(value, fallback)
    if type(value) ~= "number" or value ~= value then
        return fallback
    end
    if value > 2147483647 then return 2147483647 end
    if value < -2147483648 then return -2147483648 end
    if value >= 0 then
        return math.floor(value)
    end
    return math.ceil(value)
end

function arcade.clamp(value, minimum, maximum)
    value = integer(value, 0)
    minimum = integer(minimum, 0)
    maximum = integer(maximum, minimum)
    if maximum < minimum then
        minimum, maximum = maximum, minimum
    end
    if value < minimum then
        return minimum
    end
    if value > maximum then
        return maximum
    end
    return value
end

function arcade.actor(x, y, width, height, color)
    return {
        x = integer(x, 0),
        y = integer(y, 0),
        vx = 0,
        vy = 0,
        w = arcade.clamp(integer(width, 8), 1, 768),
        h = arcade.clamp(integer(height, 8), 1, 480),
        color = integer(color, arcade.WHITE),
        sprite = nil,
        frame = 0,
        flags = 0,
        visible = true,
        alive = true,
    }
end

function arcade.sprite_actor(asset_id, x, y, width, height)
    local actor = arcade.actor(x, y, width, height, arcade.WHITE)
    actor.sprite = integer(asset_id, 0)
    return actor
end

function arcade.move4(actor, speed, player)
    if type(actor) ~= "table" then
        return false
    end
    speed = math.max(0, integer(speed, 0))
    player = arcade.clamp(integer(player, 1), 1, 4)
    local dx = 0
    local dy = 0
    if p4.input.down(player, "left") then dx = dx - speed end
    if p4.input.down(player, "right") then dx = dx + speed end
    if p4.input.down(player, "up") then dy = dy - speed end
    if p4.input.down(player, "down") then dy = dy + speed end
    actor.x = integer(actor.x, 0) + dx
    actor.y = integer(actor.y, 0) + dy
    return dx ~= 0 or dy ~= 0
end

function arcade.step(actor)
    if type(actor) ~= "table" then
        return
    end
    actor.x = integer(actor.x, 0) + integer(actor.vx, 0)
    actor.y = integer(actor.y, 0) + integer(actor.vy, 0)
end

function arcade.step_all(actors)
    if type(actors) ~= "table" then
        return 0
    end
    local count = math.min(#actors, MAX_ACTORS)
    for index = 1, count do
        local actor = actors[index]
        if type(actor) == "table" and actor.alive ~= false then
            arcade.step(actor)
        end
    end
    return count
end

function arcade.hit(first, second)
    if type(first) ~= "table" or type(second) ~= "table" or
            first.alive == false or second.alive == false then
        return false
    end
    local ax = integer(first.x, 0)
    local ay = integer(first.y, 0)
    local aw = math.max(1, integer(first.w, 1))
    local ah = math.max(1, integer(first.h, 1))
    local bx = integer(second.x, 0)
    local by = integer(second.y, 0)
    local bw = math.max(1, integer(second.w, 1))
    local bh = math.max(1, integer(second.h, 1))
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah
end

function arcade.touching(actor, touch_index)
    if type(actor) ~= "table" then
        return false
    end
    local x, y, pressed = p4.input.touch(
        arcade.clamp(integer(touch_index, 1), 1, 5)
    )
    if not pressed then
        return false
    end
    return x >= integer(actor.x, 0) and
        x < integer(actor.x, 0) + math.max(1, integer(actor.w, 1)) and
        y >= integer(actor.y, 0) and
        y < integer(actor.y, 0) + math.max(1, integer(actor.h, 1))
end

function arcade.contain(actor, left, top, right, bottom)
    if type(actor) ~= "table" then
        return false
    end
    local width = math.max(1, integer(actor.w, 1))
    local height = math.max(1, integer(actor.h, 1))
    local old_x = integer(actor.x, 0)
    local old_y = integer(actor.y, 0)
    actor.x = arcade.clamp(old_x, integer(left, 0), integer(right, 768) - width)
    actor.y = arcade.clamp(old_y, integer(top, 0), integer(bottom, 480) - height)
    return actor.x ~= old_x or actor.y ~= old_y
end

function arcade.bounce(actor, left, top, right, bottom)
    if type(actor) ~= "table" then
        return false
    end
    left = integer(left, 0)
    top = integer(top, 0)
    right = integer(right, 768)
    bottom = integer(bottom, 480)
    local width = math.max(1, integer(actor.w, 1))
    local height = math.max(1, integer(actor.h, 1))
    local bounced = false
    if integer(actor.x, 0) < left then
        actor.x = left
        actor.vx = math.abs(integer(actor.vx, 0))
        bounced = true
    elseif integer(actor.x, 0) + width > right then
        actor.x = right - width
        actor.vx = -math.abs(integer(actor.vx, 0))
        bounced = true
    end
    if integer(actor.y, 0) < top then
        actor.y = top
        actor.vy = math.abs(integer(actor.vy, 0))
        bounced = true
    elseif integer(actor.y, 0) + height > bottom then
        actor.y = bottom - height
        actor.vy = -math.abs(integer(actor.vy, 0))
        bounced = true
    end
    return bounced
end

function arcade.wrap(actor, left, top, right, bottom)
    if type(actor) ~= "table" then
        return false
    end
    left = integer(left, 0)
    top = integer(top, 0)
    right = integer(right, 768)
    bottom = integer(bottom, 480)
    local width = math.max(1, integer(actor.w, 1))
    local height = math.max(1, integer(actor.h, 1))
    local wrapped = false
    if integer(actor.x, 0) + width < left then
        actor.x = right
        wrapped = true
    elseif integer(actor.x, 0) > right then
        actor.x = left - width
        wrapped = true
    end
    if integer(actor.y, 0) + height < top then
        actor.y = bottom
        wrapped = true
    elseif integer(actor.y, 0) > bottom then
        actor.y = top - height
        wrapped = true
    end
    return wrapped
end

function arcade.draw(actor)
    if type(actor) ~= "table" or actor.alive == false or actor.visible == false then
        return false
    end
    if actor.sprite ~= nil then
        p4.screen.sprite(
            integer(actor.sprite, 0), integer(actor.x, 0), integer(actor.y, 0),
            integer(actor.frame, 0), integer(actor.flags, 0)
        )
    else
        p4.screen.rect(
            integer(actor.x, 0), integer(actor.y, 0),
            math.max(1, integer(actor.w, 1)), math.max(1, integer(actor.h, 1)),
            integer(actor.color, arcade.WHITE), true
        )
    end
    return true
end

function arcade.draw_all(actors)
    if type(actors) ~= "table" then
        return 0
    end
    local count = math.min(#actors, MAX_ACTORS)
    local drawn = 0
    for index = 1, count do
        if arcade.draw(actors[index]) then
            drawn = drawn + 1
        end
    end
    return drawn
end

function arcade.compact(actors)
    if type(actors) ~= "table" then
        return 0
    end
    local write = 1
    local count = math.min(#actors, MAX_ACTORS)
    for read = 1, count do
        local actor = actors[read]
        if type(actor) == "table" and actor.alive ~= false then
            actors[write] = actor
            write = write + 1
        end
    end
    for index = write, count do
        actors[index] = nil
    end
    return write - 1
end

function arcade.timer(period)
    period = math.max(1, integer(period, 1))
    return { period = period, left = period }
end

function arcade.tick(timer)
    if type(timer) ~= "table" then
        return false
    end
    timer.period = math.max(1, integer(timer.period, 1))
    timer.left = integer(timer.left, timer.period) - 1
    if timer.left > 0 then
        return false
    end
    timer.left = timer.period
    return true
end

function arcade.scene(name)
    return { name = tostring(name or "title"), age = 0 }
end

function arcade.scene_set(scene, name)
    if type(scene) ~= "table" then
        return
    end
    scene.name = tostring(name or "title")
    scene.age = 0
end

function arcade.scene_tick(scene)
    if type(scene) ~= "table" then
        return 0
    end
    scene.age = integer(scene.age, 0) + 1
    return scene.age
end

local effects = {
    click = { 1, 880, 2, 150, "square" },
    jump = { 1, 660, 5, 175, "triangle" },
    hit = { 2, 110, 7, 210, "square" },
    score = { 2, 1320, 6, 190, "triangle" },
    fail = { 3, 90, 14, 190, "saw" },
}

function arcade.sfx(name)
    local effect = effects[name]
    if effect == nil then
        return false
    end
    return p4.audio.tone(
        effect[1], effect[2], effect[3], effect[4], effect[5]
    )
end

function arcade.song(notes, step_ticks, volume, waveform)
    return {
        notes = type(notes) == "table" and notes or {},
        step = math.max(1, integer(step_ticks, 8)),
        volume = arcade.clamp(integer(volume, 120), 0, 255),
        waveform = waveform or "triangle",
        index = 1,
        left = 0,
    }
end

function arcade.song_tick(song)
    if type(song) ~= "table" or #song.notes == 0 then
        return false
    end
    song.left = integer(song.left, 0) - 1
    if song.left > 0 then
        return false
    end
    local index = arcade.clamp(integer(song.index, 1), 1, #song.notes)
    local frequency = math.max(0, integer(song.notes[index], 0))
    song.index = index % #song.notes + 1
    song.left = math.max(1, integer(song.step, 8))
    if frequency == 0 then
        return true
    end
    return p4.audio.tone(
        4, frequency, song.left, arcade.clamp(integer(song.volume, 120), 0, 255),
        song.waveform or "triangle"
    )
end

return arcade

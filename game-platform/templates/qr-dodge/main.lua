local q, s, input, random = p4.arcade, p4.screen, p4.input, p4.random
local game = {}
local player = q.actor(354, 426, 60, 18, q.LIGHT_CYAN)
local rocks, spawn = {}, q.timer(24)
local mode, score, lives = "title", 0, 3

local function restart()
    player.x, player.y = 354, 426
    rocks, spawn, score, lives = {}, q.timer(24), 0, 3
    mode = "play"
end

local function add_rock()
    if #rocks >= 32 then return end
    local size = random.range(18, 42)
    local rock = q.actor(random.range(8, 760 - size), -size, size, size, q.LIGHT_RED)
    rock.vy = random.range(3, 7)
    rocks[#rocks + 1] = rock
end

function game.start()
    random.seed(0x51445247)
end

function game.update()
    if mode ~= "play" then
        if input.pressed(1, "a") then restart() end
        return
    end

    q.move4(player, 7)
    q.contain(player, 0, 50, 768, 478)
    if q.tick(spawn) then add_rock() end
    q.step_all(rocks)

    for index = 1, #rocks do
        local rock = rocks[index]
        if rock.y > 480 then
            rock.alive = false
            score = score + 1
            if score % 10 == 0 then q.sfx("score") end
        elseif q.hit(player, rock) then
            rock.alive = false
            lives = lives - 1
            q.sfx("hit")
        end
    end
    q.compact(rocks)
    if lives <= 0 then
        mode = "gameover"
        q.sfx("fail")
    end
end

function game.draw()
    s.clear(q.BLACK)
    s.rect(0, 0, 768, 42, q.BLUE, true)
    s.text("QR DODGE", 18, 12, q.WHITE)
    s.text("SCORE " .. score .. "  LIVES " .. lives, 548, 12, q.YELLOW)
    if mode == "title" then
        s.text("DODGE THE RED BLOCKS", 250, 190, q.LIGHT_CYAN)
        s.text("PRESS A TO PLAY", 294, 230, q.WHITE)
    elseif mode == "gameover" then
        s.text("GAME OVER", 326, 190, q.LIGHT_RED)
        s.text("PRESS A TO TRY AGAIN", 262, 230, q.WHITE)
    else
        q.draw(player)
        q.draw_all(rocks)
    end
end

function game.stop()
    p4.audio.stop_all()
end

return game

-- SPDX-License-Identifier: MIT
-- Bounce Lab: a deliberately small P4 Cart game for first remixes.

local game = {}

local paddle_x = 314
local ball_x = 384
local ball_y = 220
local ball_dx = 4
local ball_dy = -4
local score = 0
local waiting = true

local function reset_ball()
    ball_x = paddle_x + 70
    ball_y = 414
    ball_dx = 4
    ball_dy = -4
    waiting = true
end

function game.start()
    paddle_x = 314
    score = 0
    reset_ball()
end

function game.update()
    if p4.input.down(1, "left") then
        paddle_x = math.max(24, paddle_x - 7)
    end
    if p4.input.down(1, "right") then
        paddle_x = math.min(604, paddle_x + 7)
    end

    if waiting then
        ball_x = paddle_x + 70
        if p4.input.pressed(1, "a") then
            waiting = false
            p4.audio.tone(1, 660, 5, 180, "triangle")
        end
        return
    end

    ball_x = ball_x + ball_dx
    ball_y = ball_y + ball_dy

    if ball_x <= 14 or ball_x >= 754 then
        ball_dx = -ball_dx
        p4.audio.tone(1, 330, 2, 120, "square")
    end
    if ball_y <= 54 then
        ball_dy = -ball_dy
        p4.audio.tone(1, 440, 2, 120, "square")
    end

    local over_paddle = ball_x >= paddle_x and ball_x <= paddle_x + 140
    if ball_dy > 0 and ball_y >= 424 and ball_y <= 440 and over_paddle then
        ball_y = 423
        ball_dy = -math.abs(ball_dy)
        ball_dx = ball_dx + math.floor((ball_x - (paddle_x + 70)) / 30)
        ball_dx = math.max(-8, math.min(8, ball_dx))
        if ball_dx == 0 then ball_dx = 1 end
        score = score + 1
        p4.audio.tone(1, 520 + math.min(score, 12) * 22, 4, 200, "triangle")
    end

    if ball_y > 492 then
        reset_ball()
        p4.audio.tone(1, 120, 12, 180, "saw")
    end
end

function game.draw()
    p4.screen.clear(0x0843)
    p4.screen.rect(0, 0, 768, 40, 0x18e7, true)
    p4.screen.text("BOUNCE LAB", 22, 12, 0xffff)
    p4.screen.text("SCORE " .. score, 620, 12, 0xffe0)

    p4.screen.line(12, 52, 12, 468, 0x5dff)
    p4.screen.line(755, 52, 755, 468, 0x5dff)
    p4.screen.line(12, 52, 755, 52, 0x5dff)
    p4.screen.rect(paddle_x, 438, 140, 18, 0xfca0, true)
    p4.screen.circle(ball_x, ball_y, 11, 0xffff, true)

    if waiting then
        p4.screen.text("MOVE WITH LEFT/RIGHT", 255, 210, 0xc65f)
        p4.screen.text("PRESS A TO LAUNCH", 271, 242, 0xffff)
    end
end

function game.stop()
    p4.audio.stop_all()
end

return game

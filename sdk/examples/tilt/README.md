# TILT BALL: a ball that rolls with the tilt

Tilt the board to roll the ball. Roll it into the glowing gem. The ball bounces off the walls. A hard hit shakes the
camera. The camera also leans a little toward the ball.

## What it shows

| Topic | Where in `app_tiltball.c` | Rule |
| --- | --- | --- |
| The motion sensor: `imu_ok()`, `imu_tilt_logical()` (the tilt from the zero, in g) | `update()` | [behave.md](../../docs/behave.md) section 3 |
| THE ZERO RULE: the board position at the start is the zero (`imu_set_level()` in `enter()`), and again when the settings panel goes away | `enter()`, `item_resume()` | [visual.md](../../docs/visual.md) section 6 |
| Small angles that do something: a dead zone of 0.3 degree, then 1 degree gives about 27 px in 1 s | `DEAD_ZONE`, `GAIN[]` | visual.md section 6 |
| Walls and a bounce: a circle against boxes, 5 ms steps so a fast ball does not jump over a wall | `hit_box()`, `hit_arena()` | |
| The feel: a soft camera and a shake on a hard hit | `s_cam_x`, `s_shake`, `render()` | visual.md section 6 |
| The settings panel items: a choice with a save key (SENSITIVITY) and an action (ZERO) | `ITEMS[]`, `PANEL` | behave.md section 7 |
| The battery rule: an idle ball waits 100 ms between the frames | the end of `update()` | behave.md section 9 |
| `.needs = APP_NEEDS_IMU`: the menu hides the app on the Watcher (it has no sensor) | `app_tiltball` | behave.md section 1 |

## Build

From `tfn-os/`:

```
python sdk/tools/mk_app.py sdk/examples/tilt/app_tiltball.c --sym app_tiltball --name "TILT BALL" --kinds lcd349,cores3 --nosign
```

Add `--push <board ip> --open` to send it to a board in the developer mode. The package goes to `sdk/out/tiltball/`.

## Tests

A host test of the project (not in this repo) runs 120 s of random tilt (the ball never goes into a wall), the gem, the shake and the
1 degree check. `exshot` makes the pictures and the icon frames (the tilt: the step `y TX TY`).

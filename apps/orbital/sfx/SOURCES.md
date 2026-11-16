# The sound effects of ORBITAL

All of them have the CC0 licence (public domain). They come from the PC library of the project
(not in this tree: mono, 24000 Hz, 48 kbps), made from these packs. `orb_sfx.c` has them as C data (the
boards with no SD card play them too): `python apps/orbital/tools/mk_sfx.py` (it runs
`libs/tfsfx/mk_sfx_c.py apps/orbital/sfx apps/orbital/orb_sfx ORB <names>`).

The roar of the engine and the roar of the air are not files: they are the two noise drones of the synthesizer
(`audio_drone`), so that they can go up and down with the throttle and with the heat.

| File | What | Pack | Page |
|---|---|---|---|
| ignite.mp3 | the main engine starts | OGA Boost or Launch or Thruster Sound Effect (boost_0.mp3) | https://opengameart.org/content/boost-or-launch-or-thruster-sound-effect |
| puff.mp3 | a puff of the side jets | OGA 50 CC0 Sci-Fi SFX (teleport_01.ogg) | https://opengameart.org/content/50-cc0-sci-fi-sfx |
| harpoon.mp3 | the harpoon of the tether flies out | OGA 100 CC0 SFX #2 (sfx100v2_air_03.ogg) | https://opengameart.org/content/100-cc0-sfx-2 |
| clank.mp3 | the harpoon hits a hull | Kenney Impact Sounds (impactMetal_heavy_000.ogg) | https://kenney.nl/assets/impact-sounds |
| lock.mp3 | the tether locks on the hook | OGA 100 CC0 metal and wood SFX (lock_open_01.ogg) | https://opengameart.org/content/100-cc0-metal-and-wood-sfx |
| winch.mp3 | the winch reels the rope in | OGA Chain winch sounds (winch - Marker #2.wav) | https://opengameart.org/content/chain-winch-sounds |
| creak.mp3 | the rope under a hard pull | Kenney RPG Audio (creak3.ogg) | https://kenney.nl/assets/rpg-audio |
| snap.mp3 | the rope snaps | OGA Swish - bamboo stick weapon swooshes (swosh-15.flac) | https://opengameart.org/content/swish-bamboo-stick-weapon-swhoshes |
| bump.mp3 | the tug bumps a hull | Kenney Impact Sounds (impactMetal_heavy_002.ogg) | https://kenney.nl/assets/impact-sounds |
| alarm.mp3 | the heat alarm | OGA 30 CC0 SFX loops (alarm_02.ogg) | https://opengameart.org/content/30-cc0-sfx-loops |
| beep.mp3 | a short beep of the HUD | OGA 50 CC0 retro / synth SFX (synth_beep_02.ogg) | https://opengameart.org/content/50-cc0-retro-synth-sfx |
| whoosh.mp3 | the air at a pass | OGA 100 CC0 SFX #2 (sfx100v2_air_02.ogg) | https://opengameart.org/content/100-cc0-sfx-2 |
| boom.mp3 | a ship burns up or the debris hits | OGA 50 CC0 retro / synth SFX (retro_explosion_03.ogg) | https://opengameart.org/content/50-cc0-retro-synth-sfx |
| splash.mp3 | the capsule comes down in the sea | OGA 40 CC0 water / splash / slime SFX (splash_03.ogg) | https://opengameart.org/content/40-cc0-water-splash-slime-sfx |
| debris.mp3 | the debris | OGA 75 CC0 breaking / falling / hit SFX (bfh1_metal_falling_01.ogg) | https://opengameart.org/content/75-cc0-breaking-falling-hit-sfx |
| click.mp3 | a key of a menu | Kenney UI Audio (click1.ogg) | https://kenney.nl/assets/ui-audio |
| confirm.mp3 | a goal is done | Kenney Interface Sounds (confirmation_001.ogg) | https://kenney.nl/assets/interface-sounds |
| error.mp3 | the tether cannot fire now | Kenney Interface Sounds (error_001.ogg) | https://kenney.nl/assets/interface-sounds |

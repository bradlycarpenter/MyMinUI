# Anbernic handhelds: MyMinUI setup and patches

Status as of 2026-10-20. Read this first when resuming.

## Devices and cards

| Device | TF1 (OS) | TF2 (games) |
|---|---|---|
| RG34XXSP | Stock Anbernic V1.0.6 with MyMinUI h700 `dmenu.bin` | `GAMES`, FAT32, about 58 GB, MyMinUI |
| RG40XXV | Stock Anbernic V1.1.1.0 with MyMinUI h700 `dmenu.bin` | `GAMES40`, FAT32, MyMinUI |

Both cards have the updated PS.pak `launch.sh` and `default.cfg`. Binaries in `.system/h700/bin/` (originals kept next to them as `.orig`):

- **GAMES40 (RG40XXV):** `minarch-prof5.elf` and `audioserver-rt.elf`, the current `main`.
- **GAMES (RG34XXSP):** still `minarch-final3.elf` and `audioserver-pipe8k.elf`. It lacks the 36 ms audio floor, RT audio and Hitch Logging. Copy the two newer builds over next time the card is in; audioserver only reloads after a device restart.

GAMES40 also has Gran Turismo 2 (both discs, bin/cue, `Gran Turismo 2.m3u` with Simulation first) under `Roms/Sony PlayStation (PS)/`.

Both cards came from Knulli. BIOS files, saves and states were migrated. RetroArch states were converted with `scripts/rastate.py`.

User preference: anything configurable goes in on-device menus, never in files on the card.

## Source and builds (pcbox)

- **Repo:** `~/code/MyMinUI` (shallow clone of Turro75/MyMinUI), branch `main`. Remotes: `origin` is Turro75 upstream, `fork` is `git@github.com:bradlycarpenter/MyMinUI.git` (public). All work is on `fork/main`; the old `vsync-fix` branch was merged and deleted.
- **Built binaries:** `~/code/myminui-builds/`. The current ones are `minarch-prof5.elf`, `audioserver-rt.elf` and `PS-default.cfg`.
- **Helper and patch scripts:** `~/code/minui-dist/scripts/`.
  - `stage.sh` and `stage40.sh` stage the cards.
  - `to_myminui.sh` does the Knulli migration.
  - `gba_tidy.py` renames ROMs and moves their saves to match.
  - `*_patch.py` are the source patches as applied, kept for reference; the commits are the source of truth.
  - `prof_analyze.py` and `prof_audio.py` read Hitch Logging output. They still decode the old 32-byte run record (`<5I2H8B`); the current record is 36 bytes (`<5I4H8B`, adds `audio_post_q10` and `audio_frames_in`), so update the struct before using them on new logs.
- **Toolchain:** the prebuilt h700 blob only runs on aarch64, so builds run under QEMU (`qemu-user-binfmt`). Build command:

```
cd ~/code/MyMinUI
sg docker -c "docker run --rm --platform linux/arm64 -v $PWD/workspace:/root/workspace h700-toolchain /bin/bash -c '. ~/.bashrc && cd /root/workspace/all/minarch && make PLATFORM=h700 && cd /root/workspace/h700/audioserver && make PLATFORM=h700'"
```

- **Root on pcbox:** Claude used a Herdr pane running `ssh -t pcbox sudo -i` (a different pane each session) for mounting cards and other root actions. The user has to authenticate whenever it drops; check the pane's last line is a fresh `root@pcbox` prompt before typing, or the command lands in the password prompt. Card readers are on pcbox's USB ports. Mount with `mount -o uid=1000,gid=1000 /dev/sdX1 /mnt/g40` (GAMES40 was `sdd1`); `udisksctl` over ssh is not authorized.
- **Tailscale** on the Mac may be stopped; `tailscale up` brings `pcbox` back.

### Commits on `main` (on top of upstream)

- `023002f`: core paced to the display refresh instead of the core's fps (this was the stutter cause), pipelined threaded video handoff, low-latency audio.
- `6562387`, `187b275`: RG40XXV stick calibration via `EVIOCGABS` (range −4096..4096). PWM rumble, because the motor is on/off only. Rumble Strength menu option. Resampler clamp, which fixed the crackle. PS threaded CD/SPU/dynarec defaults.
- `0262427`: controller labels now read "Standard (D-pad only)" / "DualShock (analog)". This is the PS1 analog toggle, under Options → Controls → Controller.
- `2dd4127`: PS.pak switches the SD card to the deadline scheduler and logs to `Logs/PS-io.txt`. The card was already on deadline, so this had no real effect.
- `0ec615f`, `da226d2`: "Cache Disc in RAM" option, off by default because it didn't help. The SD runtime-PM tweak was removed.
- `ee681fe`: these notes.
- `a9a743f`, `6a065ce`: hitch profiler. Per-frame records live in RAM and are written to the card only while the core is paused (menu, sleep, close), so logging doesn't touch the SD card mid-game.
- `3f9ec86`: audio queue floor raised to 36 ms. SDL drains the queue in about 21 ms chunks, so the old 24 ms target ran dry and caused the GT2 race crackle/hitch.
- `e497bd5`: **Hitch Logging** menu toggle (Options → Frontend, off by default, starts a new segment each time it's switched on). Audio in/out levels per frame. audioserver logs STARVE / XRUN / SHORT / LOW events to `/tmp/audioserver-events.log` and replays the chunk lost on an xrun.
- `de76ed7`: `SCHED_FIFO` for the SDL audio thread (prio 18) and audioserver (prio 20); audioserver ALSA target 32 ms with a 64 ms buffer. This fixed the remaining GT2 in-race audio hitch, which was a ~40 ms stall of the SDL audio thread while emulation itself was fine.

### Key code locations

- **`workspace/h700/platform/platform.c`:**
  - vsync: waits on the display interrupt counter from `/sys/class/disp/disp/attr/sys`, because `FBIO_WAITFORVSYNC` is a stub on this kernel. Uses 3 ION buffers.
  - the lid polling thread
  - stick calibration
  - PWM rumble
- **`workspace/all/common/api.c`:**
  - audio rate compensation, latency targets and the resampler clamp
  - `GFX_flip` always waits for vsync
  - `SND_promoteAudioThread` puts the SDL audio thread on `SCHED_FIFO`
- **`workspace/all/minarch/minarch.c`:**
  - `limitFF` run-count pacing against `PLAT_vsyncCounter`
  - the flip thread
  - the Rumble Strength, Cache Disc, Hitch Logging and controller label options
  - the profiler block (`ProfRun`, `ProfVsync`, `profSampler`, `PROF_flush`, `PROF_setEnabled`) sits just before `cpu_set_t coret;`
- **`workspace/h700/audioserver/audioserver.c`:** the `/tmp/dsp` FIFO is shrunk to 8 KB (it was 64 KB, about 333 ms of lag). Event logging, xrun replay, `SCHED_FIFO`, `LATENCY_TARGET_MS 32`.

### Debug switches

These are files that turn debug features on. Create the file to enable it and delete it afterwards.

- `.userdata/shared/vsync-stats`: writes vsync and frame timing stats.
- `.userdata/shared/input-debug`: logs input and rumble events.
- `.userdata/shared/vsync-mode`: forces the vsync mode to `soft`, `irq` or `fbio`.

Hitch Logging is a menu option, not a file. Its output goes to `Logs/hitch/YYYYMMDD-HHMMSS-{runs.bin,vsync.bin,sys.txt,aserver.txt}` on the games card. Sessions analysed so far are copied to pcbox `~/code/minui-dist/prof/`.

## Backups (pcbox `~/code/minui-dist/`)

| Backup | Contents |
|---|---|
| `knulli-share-backup` | RG34XXSP Knulli OS card share |
| `rg40xxv-knulli-share-backup` | RG40XXV Knulli OS card share |
| `games-card-backup` | RG34XXSP games card, verified |
| `rg40xxv-games-backup` | RG40XXV games card, 12 GB, verified |
| `minui-card-snapshot` | Snapshot of the card from the earlier MinUI attempt |
| `games-saves-before-tidy` | RG34XXSP `Saves` and `.userdata/shared`, taken just before the GBA tidy |
| `stockfw` | Stock firmware images |
| `new-roms` | SMA2 and SMA4 No-Intro dumps |

## Fixed

- **RG34XXSP:**
  - tearing, visual and audio stutter
  - audio desync and the half-second button-to-sound lag
  - lid sleep works
- **RG40XXV:**
  - PS1 aspect
  - stick inverted or stuck left
  - rumble stuck on max; strength now works through PWM
  - speaker crackle
  - Gran Turismo 2 in-race audio hitches (36 ms queue floor plus RT audio threads; the last logged race had no audioserver events and no vsync misses)
- **GBA library on the RG34XXSP:**
  - removed 20 all-zero ROMs (they were already blank on the Knulli card)
  - added SMA2 and SMA4 (USA) dumps
  - stripped the "NN. " prefixes and moved their saves, states, box art and the recent list to match

## Open items and ideas

- **RG34XXSP build:** not yet updated to the current `main` (see Devices and cards).
- **`core.run` wall time** in GT2 is about 16.7 ms with ~78 voluntary context switches per frame, because the core now waits in the video callback's `bb_in_use` loop rather than in `limitFF`. Judged benign (the core is one frame ahead), but worth a look if new hitches appear.
- **PS1 scene-transition hitches** (Gran Turismo menus, the pre-race hitch): the cause is on the CPU side, most likely dynarec recompiles, not disc I/O. Neither the scheduler change nor the disc cache helped. The user accepts that these are probably unavoidable.
  - Untried: the `PhysCdPropagations` hack in PCSX ReARMed's `cdrom.c` (r25, 6365a756), which sits behind `#if 0` there.
- **In-game analog toggle combo** (L1+R1+Select): not verified.
- **Blank ROMs elsewhere:** the RG40XXV card and the PS1 games haven't been checked. Saves for the 20 removed GBA games are kept under clean No-Intro names, so re-adding a ROM with the same name picks its save back up.
- **Original ROMs:** pcbox drives haven't been searched for them.
- **Rust frontend:** the user asked about it and it was deferred.

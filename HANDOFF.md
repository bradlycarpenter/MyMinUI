# Anbernic handhelds: MyMinUI setup and patches

Status as of 2026-10-18. Read this first when resuming.

## Devices and cards

| Device | TF1 (OS) | TF2 (games) |
|---|---|---|
| RG34XXSP | Stock Anbernic V1.0.6 with MyMinUI h700 `dmenu.bin` | `GAMES`, FAT32, about 58 GB, MyMinUI |
| RG40XXV | Stock Anbernic V1.1.1.0 with MyMinUI h700 `dmenu.bin` | `GAMES40`, FAT32, MyMinUI |

Both games cards run the patched build: `minarch-final3.elf` and `audioserver-pipe8k.elf`, plus the updated PS.pak `launch.sh` and `default.cfg`. The originals are kept next to them as `.orig`.

Both cards came from Knulli. BIOS files, saves and states were migrated. RetroArch states were converted with `scripts/rastate.py`.

User preference: anything configurable goes in on-device menus, never in files on the card.

## Source and builds (pcbox)

- **Repo:** `~/code/MyMinUI` (shallow clone of Turro75/MyMinUI), branch `vsync-fix`.
- **Built binaries:** `~/code/myminui-builds/`. The current ones are `minarch-final3.elf`, `audioserver-pipe8k.elf` and `PS-default.cfg`.
- **Helper and patch scripts:** `~/code/minui-dist/scripts/`.
  - `stage.sh` and `stage40.sh` stage the cards.
  - `to_myminui.sh` does the Knulli migration.
  - `gba_tidy.py` renames ROMs and moves their saves to match.
- **Toolchain:** the prebuilt h700 blob only runs on aarch64, so builds run under QEMU (`qemu-user-binfmt`). Build command:

```
cd ~/code/MyMinUI
sg docker -c "docker run --rm --platform linux/arm64 -v $PWD/workspace:/root/workspace h700-toolchain /bin/bash -c '. ~/.bashrc && cd /root/workspace/all/minarch && make PLATFORM=h700'"
```

- **Root on pcbox:** Claude used a Herdr pane running `sudo -i` (a different pane each session) for mounting cards and other root actions. Card readers are on pcbox's USB ports.

### Commits on `vsync-fix`

- `023002f`: core paced to the display refresh instead of the core's fps (this was the stutter cause), pipelined threaded video handoff, low-latency audio.
- `6562387`, `187b275`: RG40XXV stick calibration via `EVIOCGABS` (range −4096..4096). PWM rumble, because the motor is on/off only. Rumble Strength menu option. Resampler clamp, which fixed the crackle. PS threaded CD/SPU/dynarec defaults.
- `0262427`: controller labels now read "Standard (D-pad only)" / "DualShock (analog)". This is the PS1 analog toggle, under Options → Controls → Controller.
- `2dd4127`: PS.pak switches the SD card to the deadline scheduler and logs to `Logs/PS-io.txt`. The card was already on deadline, so this had no real effect.
- `0ec615f`, `da226d2`: "Cache Disc in RAM" option, off by default because it didn't help. The SD runtime-PM tweak was removed.

### Key code locations

- **`workspace/h700/platform/platform.c`:**
  - vsync: waits on the display interrupt counter from `/sys/class/disp/disp/attr/sys`, because `FBIO_WAITFORVSYNC` is a stub on this kernel. Uses 3 ION buffers.
  - the lid polling thread
  - stick calibration
  - PWM rumble
- **`workspace/all/common/api.c`:**
  - audio rate compensation, latency targets and the resampler clamp
  - `GFX_flip` always waits for vsync
- **`workspace/all/minarch/minarch.c`:**
  - `limitFF` run-count pacing against `PLAT_vsyncCounter`
  - the flip thread
  - the Rumble Strength, Cache Disc and controller label options
- **`workspace/h700/audioserver/audioserver.c`:** the `/tmp/dsp` FIFO is shrunk to 8 KB (it was 64 KB, about 333 ms of lag).

### Debug switches

These are files that turn debug features on. Create the file to enable it and delete it afterwards.

- `.userdata/shared/vsync-stats`: writes vsync and frame timing stats.
- `.userdata/shared/input-debug`: logs input and rumble events.
- `.userdata/shared/vsync-mode`: forces the vsync mode to `soft`, `irq` or `fbio`.

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
- **GBA library on the RG34XXSP:**
  - removed 20 all-zero ROMs (they were already blank on the Knulli card)
  - added SMA2 and SMA4 (USA) dumps
  - stripped the "NN. " prefixes and moved their saves, states, box art and the recent list to match

## Open items and ideas

- **PS1 scene-transition hitches** (Gran Turismo menus, the pre-race hitch): the cause is on the CPU side, most likely dynarec recompiles, not disc I/O. Neither the scheduler change nor the disc cache helped. The user accepts that these are probably unavoidable.
  - Untried: the `PhysCdPropagations` hack in PCSX ReARMed's `cdrom.c` (r25, 6365a756), which sits behind `#if 0` there.
- **In-game analog toggle combo** (L1+R1+Select): not verified.
- **Blank ROMs elsewhere:** the RG40XXV card and the PS1 games haven't been checked. Saves for the 20 removed GBA games are kept under clean No-Intro names, so re-adding a ROM with the same name picks its save back up.
- **Original ROMs:** pcbox drives haven't been searched for them.
- **Rust frontend:** the user asked about it and it was deferred.

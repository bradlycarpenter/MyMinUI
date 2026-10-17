#!/bin/sh

EMU_EXE=pcsx_rearmed

RUN=minarch
RUN2=minarch


#CPU_OC=${CPU_SPEED_MAX} # 1.512 GHz
CPU_OC=${CPU_SPEED_PERF}  # 1.344 GHz
#CPU_OC=${CPU_SPEED_GAME}  # 1.2 GHz
#CPU_OC=${CPU_SPEED_POWERSAVE}  # 816MHz
#CPU_OC=${CPU_SPEED_MENU}  # 576MHz

CURDIR="$(dirname "$0")"
#$1 is the rom filename
#$2 is the save state slot number
#$3 is the save state file
#$4 is 1 if SELECT is pressed then use RUN2 instead of RUN
#$RUN is the deafult libretro frontend for this core.pak
#$RUN2 is the alternative libretro frontend
#"$(dirname "$0")" is the current directory
#$EMU_EXE is the name of the core to be launched
#$CPU_OC sets the desired level of cpu clock
#echo launch_rom.sh  "$1" "$2" "$3" "$4" ${RUN} ${RUN2} "$CURDIR" "$EMU_EXE" $CPU_OC > $CURDIR/this.log
SD_DEV=$(awk '$2=="/mnt/sdcard"{print $1}' /proc/mounts | sed 's|/dev/||; s|p[0-9]*$||')
SD_QUEUE=/sys/block/$SD_DEV/queue
IO_LOG="$LOGS_PATH/PS-io.txt"
if [ -n "$SD_DEV" ] && [ -d "$SD_QUEUE" ]; then
	{
		echo "before: dev=$SD_DEV scheduler=$(cat $SD_QUEUE/scheduler) read_ahead_kb=$(cat $SD_QUEUE/read_ahead_kb) max_sectors_kb=$(cat $SD_QUEUE/max_sectors_kb)"
		for c in /sys/bus/mmc/devices/*/power/control; do echo "$c: $(cat $c) $(cat ${c%control}runtime_status)"; done
		grep -E 'MemTotal|MemAvailable|^Cached' /proc/meminfo
	} > "$IO_LOG" 2>&1
	# CFQ gives each thread its own time slice on the card, so the emulator's urgent
	# disc read can wait ~100ms behind the core's read-ahead thread; deadline serves
	# reads by age instead
	SCHED=noop
	grep -q deadline "$SD_QUEUE/scheduler" && SCHED=deadline
	sudo sh -c "echo $SCHED > $SD_QUEUE/scheduler; echo 128 > $SD_QUEUE/read_ahead_kb"
	echo "after: scheduler=$(cat $SD_QUEUE/scheduler) read_ahead_kb=$(cat $SD_QUEUE/read_ahead_kb)" >> "$IO_LOG"
fi
launch_rom.sh  "$1" "$2" "$3" "$4" ${RUN} ${RUN2} "$CURDIR" "$EMU_EXE" $CPU_OC
overclock.elf ${CPU_SPEED_MENU}


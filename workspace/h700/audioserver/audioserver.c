#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <errno.h>

// timing events for minarch's Hitch Logging, which collects and truncates this file;
// it lives on tmpfs and is only written when something goes wrong
#define EVENT_LOG_PATH "/tmp/audioserver-events.log"
#define EVENT_STARVE_US 15000
#define EVENT_LOW_FRAMES (SAMPLING_FREQ * 5 / 1000)

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void log_event(const char* kind, long a, long b) {
    FILE* f = fopen(EVENT_LOG_PATH, "a");
    if (!f) return;
    fprintf(f, "%llu %s %ld %ld\n", (unsigned long long)now_ns(), kind, a, b);
    fclose(f);
}

#define FIFO_PATH "/tmp/dsp"
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
// the default 64KB pipe holds ~333ms of 48kHz stereo; since the ALSA side is kept
// at LATENCY_TARGET_MS the pipe sits full and that becomes audible input lag
#define FIFO_SIZE 8192
#define BUFFER_SIZE 1024 

#define SAMPLING_FREQ     48000
// must stay above the ~21ms chunks SDL writes into the FIFO, or one late chunk
// drains the device; the ALSA buffer below leaves room on top of it
#define LATENCY_TARGET_MS 32
#define BUSY_THRESHOLD    ((SAMPLING_FREQ * LATENCY_TARGET_MS) / 1000) 

// 🎯 STATE TRACKING VARIABLES
static volatile sig_atomic_t route_changed = 0;
static volatile sig_atomic_t target_output_hdmi = 0; // 0 = Speaker, 1 = HDMI
static int current_output_hdmi = 0;                  // 0 = Speaker, 1 = HDMI (Tracks actual hardware state)

void handle_audio_route_signal(int sig) {
    if (sig == SIGUSR1) {       
        target_output_hdmi = 1;
        route_changed = 1;
    } else if (sig == SIGUSR2) { 
        target_output_hdmi = 0;
        route_changed = 1;
    }
}

int configure_alsa_device(snd_pcm_t **handle, const char *dev_name) {
    if (*handle != NULL) {
        snd_pcm_close(*handle);
        *handle = NULL;
    }
    
    printf("[AUDIO SERVER] Opening ALSA device target: %s\n", dev_name);
    if (snd_pcm_open(handle, dev_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        fprintf(stderr, "[AUDIO SERVER ERROR] Failed to open device: %s\n", dev_name);
        return -1;
    }
    
    if (snd_pcm_set_params(*handle, SND_PCM_FORMAT_S16_LE, 
                           SND_PCM_ACCESS_RW_INTERLEAVED, 2, SAMPLING_FREQ, 1, 64000) < 0) {
        fprintf(stderr, "[AUDIO SERVER ERROR] Hardware parameters initialization failed on %s\n", dev_name);
        snd_pcm_close(*handle);
        *handle = NULL;
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    snd_pcm_t *alsa_handle = NULL;
    char buffer[BUFFER_SIZE];
    
    signal(SIGUSR1, handle_audio_route_signal);
    signal(SIGUSR2, handle_audio_route_signal);
    struct sched_param param = {.sched_priority = 20};
    if (sched_setscheduler(0, SCHED_FIFO, &param) < 0) perror("[AUDIO SERVER] SCHED_FIFO");

    // Initial state: Booting into Speaker mode (Card 0)
    if (configure_alsa_device(&alsa_handle, "default") < 0) {
        return 1;
    }
    current_output_hdmi = 0; 
    
    printf("[AUDIO SERVER] Initialized (Jitter-Free State-Aware Backpressure Mode)\n");
    unlink(FIFO_PATH);
    if (mkfifo(FIFO_PATH, 0666) < 0) {
        snd_pcm_close(alsa_handle);
        return 1;
    }

    int fifo_fd = open(FIFO_PATH, O_RDWR);
    if (fifo_fd < 0) {
        snd_pcm_close(alsa_handle);
        return 1;
    }
    int pipe_size = fcntl(fifo_fd, F_SETPIPE_SZ, FIFO_SIZE);
    printf("[AUDIO SERVER] FIFO size: %d\n", pipe_size);
    uint64_t last_read_ns = 0;
    int low = 0;

    while (1) {
        // 🎯 THE FIX: Check route only if signaled AND the requested target differs from actual current state
        if (route_changed) {
            route_changed = 0; // Clear signal flag immediately
            
            if (target_output_hdmi != current_output_hdmi) {
                printf("[AUDIO SERVER] ⚡ Executing real hardware switch: Current=%d -> Target=%d\n", 
                       current_output_hdmi, target_output_hdmi);
                
                if (target_output_hdmi == 1) {
                    if (configure_alsa_device(&alsa_handle, "hw:2,0") == 0) {
                        current_output_hdmi = 1; // Update actual tracking state on success
                    } else {
                        // Fallback to speaker if HDMI hardware allocation fails
                        configure_alsa_device(&alsa_handle, "default");
                        current_output_hdmi = 0;
                    }
                } else {
                    if (configure_alsa_device(&alsa_handle, "default") == 0) {
                        current_output_hdmi = 0; // Update actual tracking state
                    }
                }
            } else {
                // Ignore the signal if we are already streaming to the correct interface
                // This eliminates the periodic 1-second audio "bump" completely!
                printf("[AUDIO SERVER] Switch ignored. Already routed to requested output (%d).\n", current_output_hdmi);
            }
        }

        ssize_t bytes_read = read(fifo_fd, buffer, BUFFER_SIZE);
        
        if (bytes_read > 0) {
            snd_pcm_uframes_t frames = bytes_read / 4; 
            uint64_t t = now_ns();
            if (last_read_ns && t - last_read_ns > EVENT_STARVE_US * 1000ULL) log_event("STARVE", (long)((t - last_read_ns) / 1000), (long)bytes_read);
            last_read_ns = t;
            
            if (alsa_handle != NULL) {
                snd_pcm_sframes_t written = snd_pcm_writei(alsa_handle, buffer, frames);
                if (written < 0) {
                    log_event("XRUN", (long)written, (long)frames);
                    snd_pcm_prepare(alsa_handle); 
                    // the chunk that hit the underrun would otherwise be dropped
                    snd_pcm_writei(alsa_handle, buffer, frames);
                }

                // MINUI BACKPRESSURE SYNC BLOCK
                snd_pcm_sframes_t delay_frames;
                if (snd_pcm_delay(alsa_handle, &delay_frames) == 0) {
                    if (delay_frames < EVENT_LOW_FRAMES) {
                        if (!low) log_event("LOW", (long)delay_frames, 0);
                        low = 1;
                    }
                    else low = 0;
                    if (delay_frames > BUSY_THRESHOLD) {
                        snd_pcm_uframes_t extra_frames = delay_frames - BUSY_THRESHOLD;
                        useconds_t usleep_time = (uint64_t)extra_frames * 1000000 / SAMPLING_FREQ;
                        if (usleep_time > 0) {
                            usleep(usleep_time); 
                        }
                    }
                }
            }
        } else if (bytes_read == 0) {
            usleep(1000); 
        }
    }

    close(fifo_fd);
    if (alsa_handle) snd_pcm_close(alsa_handle);
    unlink(FIFO_PATH);
    return 0;
}

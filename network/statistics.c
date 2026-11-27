#include "network_impl.h"
#include "requests.h"

#include <config_impl.h>
#include <mp.h>
#include <ds.h>
#include <bestassert.h>
#include <random.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <msquic.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <stdio.h>
    #include <string.h>
#endif

#define ctx network_ctx

static u64 get_free_ram(void) {
#ifdef _WIN32
    MEMORYSTATUSEX st = { .dwLength = sizeof(st) };
    return GlobalMemoryStatusEx(&st) ? st.ullAvailPhys : (u64)-1;
#else
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return (u64)-1;
    char line;
    u64 avail_kb = 0;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            found = (sscanf(line + 13, "%llu", (unsigned long long *)&avail_kb) == 1);
            break;
        }
    }
    fclose(f);
    return found ? avail_kb * 1024 : (u64)-1;
#endif
}

static u64 get_raw_idle_ns(u64 ns_per_tick) {
#ifdef _WIN32
    (void)ns_per_tick;
    FILETIME ft_idle, ft_kernel, ft_user;
    if (!GetSystemTimes(&ft_idle, &ft_kernel, &ft_user)) return (u64)-1;
    u64 idle = ((u64)ft_idle.dwHighDateTime << 32) | ft_idle.dwLowDateTime;
    return idle * 100ULL;
#else
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return (u64)-1;
    u64 u, n, s, id, io = 0;
    int scanned = fscanf(f, "cpu %llu %llu %llu %llu %llu",
                         (unsigned long long *)&u, (unsigned long long *)&n,
                         (unsigned long long *)&s, (unsigned long long *)&id,
                         (unsigned long long *)&io);
    fclose(f);
    if (scanned < 4) return (u64)-1;
    return (id + io) * ns_per_tick;
#endif
}

void share_statistic_worker(void *arg) {
    u64 ns_per_tick = 100;
#ifndef _WIN32
    long clk_tck = sysconf(_SC_CLK_TCK);
    ns_per_tick = (clk_tck > 0) ? (1000000000ULL / (u64)clk_tck) : 0;
#endif

    u64 prev_time = get_time();
    u64 prev_idle = (ns_per_tick > 0) ? get_raw_idle_ns(ns_per_tick) : (u64)-1;

    while (atomic_load_explicit(&ctx.stat_running, memory_order_relaxed)) {
        mp_sleep(ctx.stat_share_interval);

        u64 now = get_time();
        u64 cur_idle = (ns_per_tick > 0) ? get_raw_idle_ns(ns_per_tick) : (u64)-1;

        double idle_power = -1.0;
        if (cur_idle != (u64)-1 && prev_idle != (u64)-1 && now > prev_time) {
            idle_power = (double)(cur_idle - prev_idle) / (double)(now - prev_time);
        }
        prev_time = now;
        prev_idle = cur_idle;

        u64 free_ram = get_free_ram();
        u64 free_disk = -1;

        rwl_write(&ctx.stat_local_lock);
        ctx.stat.idle_power = idle_power;
        ctx.stat.free_ram = free_ram;
        ctx.stat.free_disk = free_disk;
        rwl_unwrite(&ctx.stat_local_lock);

        RequestShareStatistic req = {
            .request_type = SHARE_STATISTIC,
            .stat = {
                .idle_power = idle_power,
                .free_ram = free_ram,
                .free_disk = free_disk,
            },
        };
        
        Peer **peers = NULL;
        rwl_read(&ctx.peers_lock);
        i64 count = hmlen(ctx.peers);
        arrsetcap(peers, count);
        for (i64 i = 0; i < count; ++i) {
            Peer *p = ctx.peers[i].value;
            if (ref_peer(p)) {
                arrput(peers, p);
            }
        }
        rwl_unread(&ctx.peers_lock);

        count = arrlen(peers);
        for (i64 i = 0; i < count; ++i) {    
            Channel *control = get_control_channel(peers[i]);
            if (control == NULL) {
                continue;
            }
            channel_send(control, &req, sizeof(req), false);
        }
        
        for (i64 i = 0; i < count; ++i) {
            unref_peer(peers[i]);
        }
        arrfree(peers);
    }
}


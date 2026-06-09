#include "jfm.h"
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#define POLL_INTERVAL 3600  // 1 hour between polls
#define STALE_THRESHOLD 86400  // 24 hours

volatile sig_atomic_t should_exit = 0;

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        should_exit = 1;
    }
}

void check_and_poll_series(sqlite3 *db, Server *server, Series *series) {
    time_t now = time(NULL);
    
    // Skip if recently polled
    if (series->last_polled > 0 && (now - series->last_polled) < POLL_INTERVAL) {
        return;
    }
    
    char *json = jf_get_series_episodes(server->url, server->api_key, series->jellyfin_id);
    
    if (!json) {
        syslog(LOG_WARNING, "Failed to poll series %s from %s", series->name, server->name);
        return;
    }
    
    Episode episodes[500];
    int count = 0;
    
    if (jf_parse_episodes(json, episodes, &count) == 0) {
        for (int i = 0; i < count; i++) {
            db_add_episode(db, server->id, atoi(series->id), 
                          episodes[i].jellyfin_id,
                          episodes[i].name,
                          episodes[i].season,
                          episodes[i].episode,
                          episodes[i].runtime_ticks);
        }
        
        db_update_series_poll_time(db, series->id);
        syslog(LOG_INFO, "Polled series %s: found %d episodes", series->name, count);
    }
    
    json_free(json);
}

int main(int argc, char *argv[]) {
    int daemonize = 1;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            daemonize = 0;
        }
    }
    
    sqlite3 *db = db_open(DB_PATH);
    if (!db) {
        fprintf(stderr, "Failed to open database\n");
        return 1;
    }
    
    openlog("jfm-sync", LOG_PID, LOG_DAEMON);
    
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    syslog(LOG_INFO, "Starting Jellyfin sync daemon");
    
    while (!should_exit) {
        Server servers[MAX_SERVERS];
        int server_count = db_get_servers(db, servers, MAX_SERVERS);
        
        for (int s = 0; s < server_count; s++) {
            Series series_list[100];
            int series_count = db_get_series(db, servers[s].id, series_list, 100);
            
            for (int i = 0; i < series_count; i++) {
                check_and_poll_series(db, &servers[s], &series_list[i]);
            }
        }
        
        sleep(POLL_INTERVAL / 10);  // Check every 6 minutes for shutdown signal
    }
    
    syslog(LOG_INFO, "Shutting down Jellyfin sync daemon");
    closelog();
    db_close(db);
    
    return 0;
}

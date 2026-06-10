#ifndef JFM_H
#define JFM_H

#include <sqlite3.h>
#include <time.h>

#define MAX_SERVERS 10
#define MAX_PATH 1024
#define MAX_URL 512
#define MAX_API_KEY 256
#define MAX_QUERY 256
#define JELLYFIN_AGENT_NAME "jellyfin-media-manager"
#define DB_PATH "/home/xanth/.local/share/jfm/jfm.db"
#define VIDEO_ROOT "/data/video"

typedef struct {
    int id;
    char name[256];
    char url[MAX_URL];
    char api_key[MAX_API_KEY];
    int enabled;
} Server;

typedef struct {
    char id[256];
    char name[512];
    char jellyfin_id[256];
    int server_id;
    time_t last_polled;
} Series;

typedef struct {
    char id[256];
    char name[512];
    char jellyfin_id[256];
    int series_id;
    int server_id;
    int season;
    int episode;
    long runtime_ticks;
    int is_aired;
    time_t added_date;
} Episode;

typedef struct {
    char path[MAX_PATH];
    char series_name[512];
    int season;
    int episode;
    int duration_seconds;
} LocalFile;

// Function declarations
sqlite3 *db_open(const char *path);
void db_close(sqlite3 *db);
int db_add_server(sqlite3 *db, const char *name, const char *url, const char *api_key);
int db_get_servers(sqlite3 *db, Server *servers, int max_count);
int db_get_series(sqlite3 *db, int server_id, Series *series, int max_count);
int db_add_series(sqlite3 *db, const char *name, int server_id, const char *jellyfin_id);
int db_add_episode(sqlite3 *db, int server_id, int series_id, const char *jellyfin_id,
                   const char *name, int season, int episode, long runtime_ticks);
int db_mark_watched(sqlite3 *db, const char *file_id, const char *user, int is_finished, double position);
int db_get_watch_state(sqlite3 *db, const char *file_id, const char *user, double *position, int *is_finished);
int db_add_local_file(sqlite3 *db, const char *path, const char *series_name, int season, int episode, int duration);
int db_is_path_excluded(sqlite3 *db, const char *path);
int db_add_exclude_path(sqlite3 *db, const char *path);

// Jellyfin API functions
char *jf_search(const char *server_url, const char *api_key, const char *query);
char *jf_get_series_episodes(const char *server_url, const char *api_key, const char *series_id);
char *jf_get_item(const char *server_url, const char *api_key, const char *item_id);
// void jf_parse_search_results(const char *json, Episode *episodes, int *count);
int jf_parse_search_results(const char *json, Episode *episodes, int *count);

void json_free(char *json);
// Add after the existing function declarations:
int jf_parse_episodes(const char *json, Episode *episodes, int *count);
int db_update_series_poll_time(sqlite3 *db, const char *series_id);
// Database query functions (add these)
int db_get_series_by_id(sqlite3 *db, const char *jellyfin_id, Series *series);
int db_get_episodes_for_series(sqlite3 *db, const char *series_id, Episode *episodes, int max_count);
int db_get_local_files(sqlite3 *db, const char *series_name, LocalFile *files, int max_count);

// Logging functions (add these)
void jf_api_init_logging(const char *filename);
void jf_api_cleanup_logging(void);


#endif

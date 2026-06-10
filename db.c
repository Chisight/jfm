#include "jfm.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

sqlite3 *db_open(const char *path) {
    sqlite3 *db;
    int rc = sqlite3_open(path, &db);
    if (rc) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

void db_close(sqlite3 *db) {
    sqlite3_close(db);
}

int db_add_server(sqlite3 *db, const char *name, const char *url, const char *api_key) {
    const char *sql = "INSERT INTO servers (name, url, api_key) VALUES (?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, url, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, api_key, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? sqlite3_last_insert_rowid(db) : -1;
}

int db_get_servers(sqlite3 *db, Server *servers, int max_count) {
    const char *sql = "SELECT id, name, url, api_key, enabled FROM servers WHERE enabled = 1";
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        servers[count].id = sqlite3_column_int(stmt, 0);
        strncpy(servers[count].name, (char *)sqlite3_column_text(stmt, 1), 255);
        strncpy(servers[count].url, (char *)sqlite3_column_text(stmt, 2), MAX_URL - 1);
        strncpy(servers[count].api_key, (char *)sqlite3_column_text(stmt, 3), MAX_API_KEY - 1);
        servers[count].enabled = sqlite3_column_int(stmt, 4);
        count++;
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int db_add_series(sqlite3 *db, const char *name, int server_id, const char *jellyfin_id) {
    const char *sql = "INSERT OR IGNORE INTO series (id, server_id, name, jellyfin_id) "
                      "VALUES (lower(hex(randomblob(16))), ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, server_id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, jellyfin_id, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_add_episode(sqlite3 *db, int server_id, int series_id, const char *jellyfin_id,
                   const char *name, int season, int episode, long runtime_ticks) {
    const char *sql = "INSERT OR IGNORE INTO episodes "
                      "(id, series_id, server_id, jellyfin_id, name, season_number, episode_number, runtime_ticks) "
                      "VALUES (lower(hex(randomblob(16))), ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, series_id);
    sqlite3_bind_int(stmt, 2, server_id);
    sqlite3_bind_text(stmt, 3, jellyfin_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, season);
    sqlite3_bind_int(stmt, 6, episode);
    sqlite3_bind_int64(stmt, 7, runtime_ticks);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_mark_watched(sqlite3 *db, const char *file_id, const char *user, int is_finished, double position) {
    const char *sql = "INSERT INTO watch_history (file_id, user_name, is_finished, playback_position, watched_at) "
                      "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP) "
                      "ON CONFLICT(file_id, user_name) DO UPDATE SET "
                      "is_finished=excluded.is_finished, playback_position=excluded.playback_position, watched_at=CURRENT_TIMESTAMP";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, file_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, is_finished);
    sqlite3_bind_double(stmt, 4, position);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_watch_state(sqlite3 *db, const char *file_id, const char *user, double *position, int *is_finished) {
    const char *sql = "SELECT playback_position, is_finished FROM watch_history WHERE file_id = ? AND user_name = ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, file_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    
    if (rc == SQLITE_ROW) {
        *position = sqlite3_column_double(stmt, 0);
        *is_finished = sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);
        return 0;
    }
    
    sqlite3_finalize(stmt);
    *position = 0.0;
    *is_finished = 0;
    return -1;  // Not found
}

int db_add_local_file(sqlite3 *db, const char *path, const char *series_name, 
                      int season, int episode, int duration) {
    const char *sql = "INSERT OR IGNORE INTO local_files "
                      "(id, path, series_name, season_number, episode_number, duration_seconds) "
                      "VALUES (lower(hex(randomblob(16))), ?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, series_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, season);
    sqlite3_bind_int(stmt, 4, episode);
    sqlite3_bind_int(stmt, 5, duration);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_is_path_excluded(sqlite3 *db, const char *path) {
    const char *sql = "SELECT 1 FROM exclude_paths WHERE ? LIKE path || '%' LIMIT 1";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    
    int result = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    
    return result;
}

int db_add_exclude_path(sqlite3 *db, const char *path) {
    const char *sql = "INSERT OR IGNORE INTO exclude_paths (path) VALUES (?)";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_series_by_id(sqlite3 *db, const char *jellyfin_id, Series *series) {
    const char *sql = "SELECT id, name, jellyfin_id, server_id, last_polled FROM series WHERE jellyfin_id = ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, jellyfin_id, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        strncpy(series->id, (char *)sqlite3_column_text(stmt, 0), 255);
        strncpy(series->name, (char *)sqlite3_column_text(stmt, 1), 511);
        strncpy(series->jellyfin_id, (char *)sqlite3_column_text(stmt, 2), 255);
        series->server_id = sqlite3_column_int(stmt, 3);
        series->last_polled = sqlite3_column_int64(stmt, 4);
        sqlite3_finalize(stmt);
        return 0;
    }
    
    sqlite3_finalize(stmt);
    return -1;
}

int db_update_series_poll_time(sqlite3 *db, const char *series_id) {
    const char *sql = "UPDATE series SET last_polled = CURRENT_TIMESTAMP WHERE id = ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, series_id, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_episodes_for_series(sqlite3 *db, const char *series_id, Episode *episodes, int max_count) {
    const char *sql = "SELECT id, name, jellyfin_id, season_number, episode_number, runtime_ticks "
                      "FROM episodes WHERE series_id = ? ORDER BY season_number, episode_number";
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, series_id, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        strncpy(episodes[count].id, (char *)sqlite3_column_text(stmt, 0), 255);
        strncpy(episodes[count].name, (char *)sqlite3_column_text(stmt, 1), 511);
        strncpy(episodes[count].jellyfin_id, (char *)sqlite3_column_text(stmt, 2), 255);
        episodes[count].season = sqlite3_column_int(stmt, 3);
        episodes[count].episode = sqlite3_column_int(stmt, 4);
        episodes[count].runtime_ticks = sqlite3_column_int64(stmt, 5);
        count++;
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int db_get_local_files(sqlite3 *db, const char *series_name, LocalFile *files, int max_count) {
    const char *sql = "SELECT path, series_name, season_number, episode_number, duration_seconds "
                      "FROM local_files WHERE series_name = ? ORDER BY season_number, episode_number";
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, series_name, -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        strncpy(files[count].path, (char *)sqlite3_column_text(stmt, 0), MAX_PATH - 1);
        strncpy(files[count].series_name, (char *)sqlite3_column_text(stmt, 1), 511);
        files[count].season = sqlite3_column_int(stmt, 2);
        files[count].episode = sqlite3_column_int(stmt, 3);
        files[count].duration_seconds = sqlite3_column_int(stmt, 4);
        count++;
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int db_get_series(sqlite3 *db, int server_id, Series *series, int max_count) {
    const char *sql = "SELECT id, name, jellyfin_id, server_id, last_polled FROM series WHERE server_id = ? ORDER BY name";
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, server_id);
    
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        strncpy(series[count].id, (char *)sqlite3_column_text(stmt, 0), 255);
        strncpy(series[count].name, (char *)sqlite3_column_text(stmt, 1), 511);
        strncpy(series[count].jellyfin_id, (char *)sqlite3_column_text(stmt, 2), 255);
        series[count].server_id = sqlite3_column_int(stmt, 3);
        series[count].last_polled = sqlite3_column_int64(stmt, 4);
        count++;
    }
    
    sqlite3_finalize(stmt);
    return count;
}

int db_update_series_poll_time(sqlite3 *db, const char *series_id) {
    const char *sql = "UPDATE series SET last_polled = ? WHERE id = ?";
    sqlite3_stmt *stmt;
    time_t now = time(NULL);
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, series_id, -1, SQLITE_STATIC);
    
    int result = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(stmt);
    return result;
}


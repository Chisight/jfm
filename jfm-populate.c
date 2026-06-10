#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "jfm.h"
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AUDIO_EXTENSIONS "mp3|m4a|aac|flac|ogg|wav"
#define VIDEO_EXTENSIONS "mp4|mkv|avi|mov|webm|flv|m4v|mpg|mpeg"

int extract_episode_info(const char *filename, char *series_name, int *season, int *episode) {
    if (!filename || !series_name || !season || !episode) return 0;
    
    // Remove extension
    char base[512];
    strncpy(base, filename, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    
    // Pattern: "SeriesName_S##E## ..." or "SeriesName_S##E##..."
    // The key is finding S followed by digits, E followed by digits
    char *s_ptr = strstr(base, "_S");
    if (!s_ptr) {
        s_ptr = strstr(base, " S");
    }
    if (!s_ptr) {
        return 0;  // No season marker found
    }
    
    // Try to parse season and episode
    int parsed_season = 0, parsed_episode = 0;
    int matched = sscanf(s_ptr, "%*[_]S%dE%d", &parsed_season, &parsed_episode);
    if (matched != 2) {
        // Try with space instead of underscore
        matched = sscanf(s_ptr, "%*[ ]S%dE%d", &parsed_season, &parsed_episode);
    }
    
    if (matched != 2 || parsed_season < 0 || parsed_episode < 0) {
        printf("DEBUG: Could not parse S##E## from: %s\n", s_ptr);
        return 0;
    }
    
    // Extract series name (everything before S marker)
    int name_len = s_ptr - base;
    if (name_len > 255) name_len = 255;
    strncpy(series_name, base, name_len);
    series_name[name_len] = '\0';
    
    // Clean up underscores/whitespace in series name
    for (char *p = series_name; *p; p++) {
        if (*p == '_') *p = ' ';
    }
    
    *season = parsed_season;
    *episode = parsed_episode;
    
    return 1;
}


int get_video_duration(const char *filepath, int *duration_ms) {
    if (!filepath || !duration_ms) return -1;
    
    // Use ffprobe to get duration
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1:noinput=1 '%s' 2>/dev/null",
        filepath);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "ERROR: Failed to run ffprobe for %s\n", filepath);
        return -1;
    }
    
    double duration_sec = 0.0;
    int scanned = fscanf(fp, "%lf", &duration_sec);
    pclose(fp);
    
    if (scanned != 1) {
        fprintf(stderr, "ERROR: Could not parse duration for %s\n", filepath);
        return -1;
    }
    
    *duration_ms = (int)(duration_sec * 1000);
    return 0;
}




int has_valid_extension(const char *filename, const char *extensions) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;
    
    dot++;
    char ext_lower[16] = {0};
    strncpy(ext_lower, dot, 15);
    
    for (char *p = ext_lower; *p; p++) {
        *p = tolower(*p);
    }
    
    char ext_copy[256];
    strncpy(ext_copy, extensions, 255);
    char *token = strtok(ext_copy, "|");
    while (token) {
        if (strcmp(token, ext_lower) == 0) {
            return 1;
        }
        token = strtok(NULL, "|");
    }
    
    return 0;
}

void scan_directory(sqlite3 *db, const char *path, int depth) {
    if (depth > 5) return;  // Prevent too deep recursion
    
    // Check if path is excluded
    if (db_is_path_excluded(db, path)) {
        printf("Skipping excluded path: %s\n", path);
        return;
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        perror(path);
        return;
    }
    
    struct dirent *entry;
    struct stat stat_buf;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char filepath[MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        
        if (stat(filepath, &stat_buf) < 0) continue;
        
        if (S_ISDIR(stat_buf.st_mode)) {
            scan_directory(db, filepath, depth + 1);
        } else if (S_ISREG(stat_buf.st_mode)) {
            if (has_valid_extension(entry->d_name, VIDEO_EXTENSIONS)) {
                char series_name[512] = {0};
                int season = 0, episode = 0;
                
                if (extract_episode_info(entry->d_name, series_name, &season, &episode)) {
                    int duration = 0;
                    if (get_video_duration(filepath, &duration) == 0) {
                        printf("Found: %s - S%02dE%02d (%s)\n",
                            series_name, season, episode, filepath);
                        if (db_add_local_file(db, filepath, series_name, season, episode, duration) < 0) {
                            printf("ERROR: Failed to insert file into database\n");
                        }
                    } else {
                        printf("ERROR: Could not get duration for %s\n", filepath);
                    }
                } else {
                    // DEBUG: Show why pattern didn't match
                    printf("DEBUG: Skipped (pattern mismatch): %s\n", entry->d_name);
                }
            }
        }
    }
    
    closedir(dir);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    sqlite3 *db = db_open(DB_PATH);
    if (!db) {
        fprintf(stderr, "Failed to open database\n");
        return 1;
    }
    
    printf("Scanning %s for video files...\n", VIDEO_ROOT);
    scan_directory(db, VIDEO_ROOT, 0);
    printf("Scan complete\n");
    
    db_close(db);
    return 0;
}


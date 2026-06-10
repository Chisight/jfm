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
    // Pattern: SeriesName - S01E05 - Episode Title.mkv
    // Or: SeriesName_S01E05.mkv
    char *copy = strdup(filename);
    char *base = basename(copy);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    
    int s = 0, e = 0;
    char name[512] = {0};
    
    // Try "S##E##" pattern
    if (sscanf(base, "%511[^S]S%dE%d", name, &s, &e) >= 3) {
        // FIX: Use the correct buffer size (511), not sizeof()
        strncpy(series_name, name, 511);
        series_name[511] = '\0';
        
        // Clean up trailing punctuation
        int len = strlen(series_name);
        while (len > 0 && (series_name[len-1] == ' ' || series_name[len-1] == '-' || series_name[len-1] == '_')) {
            series_name[--len] = 0;
        }
        
        // Replace underscores with spaces
        for (char *p = series_name; *p; p++) {
            if (*p == '_') *p = ' ';
        }
        
        *season = s;
        *episode = e;
        free(copy);
        return 1;
    }
    
    free(copy);
    return 0;
}

int get_video_duration(const char *filepath, int *duration_seconds) {
    // Use ffprobe to get duration - properly escape the filepath
    char *escaped_path = malloc(strlen(filepath) * 2 + 1);
    if (!escaped_path) return -1;
    
    char *src = (char *)filepath;
    char *dst = escaped_path;
    
    // Escape single quotes and backslashes for shell
    while (*src) {
        if (*src == '\'') {
            *dst++ = '\'';
            *dst++ = '\\';
            *dst++ = '\'';
            *dst++ = '\'';
            src++;
        } else if (*src == '\\') {
            *dst++ = '\\';
            *dst++ = '\\';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1:noprint_filename=1 '%s' 2>/dev/null",
        escaped_path);
    
    FILE *fp = popen(cmd, "r");
    free(escaped_path);
    
    if (!fp) return -1;
    
    double duration = 0;
    int result = fscanf(fp, "%lf", &duration);
    pclose(fp);
    
    if (result != 1) return -1;
    
    *duration_seconds = (int)duration;
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
                        db_add_local_file(db, filepath, series_name, season, episode, duration);
                    }
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


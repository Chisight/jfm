#include "jfm.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>

#define MAX_MENU_ITEMS 100
#define MAX_DISPLAY_LINES 50

typedef enum {
    SCREEN_MAIN,
    SCREEN_SEARCH,
    SCREEN_BROWSE_SERIES,
    SCREEN_EPISODES,
    SCREEN_LOCAL_FILES,
    SCREEN_WATCH_HISTORY
} ScreenType;

typedef struct {
    char title[256];
    char data[512];  // Could be series ID, file path, etc.
    int type;        // 0=series, 1=episode, 2=file
} MenuItem;

typedef struct {
    sqlite3 *db;
    Server servers[MAX_SERVERS];
    int server_count;
    int current_server;
    ScreenType current_screen;
    MenuItem menu_items[MAX_MENU_ITEMS];
    int menu_count;
    int selected_item;
    char search_query[MAX_QUERY];
    char username[256];
} UIState;

static UIState ui_state = {0};

void init_ui(sqlite3 *db) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLACK);
        init_pair(2, COLOR_BLACK, COLOR_WHITE);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_GREEN, COLOR_BLACK);
        init_pair(5, COLOR_RED, COLOR_BLACK);
    }
    
    ui_state.db = db;
    ui_state.server_count = db_get_servers(db, ui_state.servers, MAX_SERVERS);
    ui_state.current_server = 0;
    ui_state.current_screen = SCREEN_MAIN;
    ui_state.selected_item = 0;
    strncpy(ui_state.username, getenv("USER") ? getenv("USER") : "user", 255);
}

void cleanup_ui(void) {
    endwin();
}

void draw_header(void) {
    attron(COLOR_PAIR(2));
    mvprintw(0, 0, "%-80s", "");
    mvprintw(0, 0, " Jellyfin Media Manager");
    if (ui_state.server_count > 0) {
        mvprintw(0, 50, "Server: %s", ui_state.servers[ui_state.current_server].name);
    }
    attroff(COLOR_PAIR(2));
}

void draw_footer(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    attron(COLOR_PAIR(2));
    mvprintw(max_y - 1, 0, "%-80s", "");
    mvprintw(max_y - 1, 0, " q:Quit  j/k:Select  Enter:Open  /Search  w:Watch  ?Help");
    attroff(COLOR_PAIR(2));
}

void draw_menu(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    for (int i = 1; i < max_y - 2; i++) {
        mvprintw(i, 0, "%-80s", "");
    }
    
    for (int i = 0; i < ui_state.menu_count && i < max_y - 3; i++) {
        int y = i + 1;
        if (i == ui_state.selected_item) {
            attron(COLOR_PAIR(2));
            mvprintw(y, 0, " > %-76s", ui_state.menu_items[i].title);
            attroff(COLOR_PAIR(2));
        } else {
            mvprintw(y, 0, "   %-76s", ui_state.menu_items[i].title);
        }
    }
}

void load_series_for_display(void) {
    ui_state.menu_count = 0;
    
    Series series_list[MAX_MENU_ITEMS];
    int count = db_get_series(ui_state.db, ui_state.servers[ui_state.current_server].id, 
                               series_list, MAX_MENU_ITEMS);
    
    for (int i = 0; i < count; i++) {
        snprintf(ui_state.menu_items[i].title, 256, "%s", series_list[i].name);
        strncpy(ui_state.menu_items[i].data, series_list[i].jellyfin_id, 511);
        ui_state.menu_items[i].type = 0;
        ui_state.menu_count++;
    }
    
    ui_state.selected_item = 0;
}

void load_episodes_for_series(const char *series_jellyfin_id) {
    ui_state.menu_count = 0;
    
    Series series;
    if (db_get_series_by_id(ui_state.db, series_jellyfin_id, &series) < 0) {
        return;
    }
    
    Episode episodes[MAX_MENU_ITEMS];
    int count = db_get_episodes_for_series(ui_state.db, series.id, episodes, MAX_MENU_ITEMS);
    
    for (int i = 0; i < count; i++) {
        snprintf(ui_state.menu_items[i].title, 256, "S%02dE%02d - %s", 
                 episodes[i].season, episodes[i].episode, episodes[i].name);
        strncpy(ui_state.menu_items[i].data, episodes[i].jellyfin_id, 511);
        ui_state.menu_items[i].type = 1;
        ui_state.menu_count++;
    }
    
    ui_state.selected_item = 0;
    ui_state.current_screen = SCREEN_EPISODES;
}

void load_local_files(const char *series_name) {
    ui_state.menu_count = 0;
    
    LocalFile files[MAX_MENU_ITEMS];
    int count = db_get_local_files(ui_state.db, series_name, files, MAX_MENU_ITEMS);
    
    for (int i = 0; i < count; i++) {
        char *filename = strrchr(files[i].path, '/');
        filename = filename ? filename + 1 : files[i].path;
        
        snprintf(ui_state.menu_items[i].title, 256, "S%02dE%02d - %s", 
                 files[i].season, files[i].episode, filename);
        strncpy(ui_state.menu_items[i].data, files[i].path, 511);
        ui_state.menu_items[i].type = 2;
        ui_state.menu_count++;
    }
    
    ui_state.selected_item = 0;
    ui_state.current_screen = SCREEN_LOCAL_FILES;
}

void perform_search(const char *query) {
    ui_state.menu_count = 0;
    
    Server *server = &ui_state.servers[ui_state.current_server];
    char *json = jf_search(server->url, server->api_key, query);
    
    if (!json) {
        mvprintw(10, 5, "Search failed or no results");
        refresh();
        sleep(2);
        return;
    }
    
    Episode results[MAX_MENU_ITEMS];
    int count = 0;
    jf_parse_search_results(json, results, &count);
    json_free(json);
    
    for (int i = 0; i < count; i++) {
        //snncpy(ui_state.menu_items[i].title, results[i].name, 255);
        strncpy(ui_state.menu_items[i].title, results[i].name, 255);
        strncpy(ui_state.menu_items[i].data, results[i].jellyfin_id, 511);
        ui_state.menu_items[i].type = 0;
        ui_state.menu_count++;
    }
    
    ui_state.selected_item = 0;
}

void play_video(const char *filepath) {
    char cmd[2048];
    
    double position = 0.0;
    int is_finished = 0;
    db_get_watch_state(ui_state.db, filepath, ui_state.username, &position, &is_finished);
    
    // Build mpv command with resume position
    //snprintf(cmd, sizeof(cmd), "mpv --autofit=3072x1728 --autosync=5 --mc=0 --vo=gpu", filepath);
    snprintf(cmd, sizeof(cmd), "mpv --autofit=3072x1728 --autosync=5 --mc=0 --vo=gpu %s", filepath);
    if (position > 0) {
        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " --start=%lf", position);
    }
    snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " %s", filepath);
    
    // Cleanup ncurses, play video, then reinit
    cleanup_ui();
    system(cmd);
    init_ui(ui_state.db);
    
    // Mark as watched after playing
    db_mark_watched(ui_state.db, filepath, ui_state.username, 1, 100.0);
}

void screen_main(void) {
    clear();
    draw_header();
    
    if (ui_state.menu_count == 0) {
        ui_state.menu_count = 4;
        strcpy(ui_state.menu_items[0].title, "Browse Series");
        strcpy(ui_state.menu_items[0].data, "browse");
        strcpy(ui_state.menu_items[1].title, "Search All Servers");
        strcpy(ui_state.menu_items[1].data, "search");
        strcpy(ui_state.menu_items[2].title, "Local Files");
        strcpy(ui_state.menu_items[2].data, "local");
        strcpy(ui_state.menu_items[3].title, "Watch History");
        strcpy(ui_state.menu_items[3].data, "history");
    }
    
    draw_menu();
    draw_footer();
    refresh();
}

void screen_search(void) {
    clear();
    draw_header();
    
    mvprintw(5, 5, "Enter search query:");
    mvprintw(6, 5, "> ");
    echo();
    curs_set(1);
    getnstr(ui_state.search_query, MAX_QUERY - 1);
    curs_set(0);
    noecho();
    
    perform_search(ui_state.search_query);
    ui_state.current_screen = SCREEN_SEARCH;
    
    draw_menu();
    draw_footer();
    refresh();
}

int main(void) {
    sqlite3 *db = db_open(DB_PATH);
    if (!db) {
        fprintf(stderr, "Failed to open database\n");
        return 1;
    }
    
    init_ui(db);
    
    int ch;
    ScreenType prev_screen = SCREEN_MAIN;
    
    while (1) {
        screen_main();
        
        ch = getch();
        
        switch (ch) {
            case 'q':
                goto cleanup;
            
            case 'j':
                if (ui_state.selected_item < ui_state.menu_count - 1) {
                    ui_state.selected_item++;
                }
                break;
            
            case 'k':
                if (ui_state.selected_item > 0) {
                    ui_state.selected_item--;
                }
                break;
            
            case '/':
                screen_search();
                break;
            
            case '\n':
            case KEY_ENTER:
                if (ui_state.current_screen == SCREEN_MAIN) {
                    if (strcmp(ui_state.menu_items[ui_state.selected_item].data, "browse") == 0) {
                        load_series_for_display();
                        ui_state.current_screen = SCREEN_BROWSE_SERIES;
                    } else if (strcmp(ui_state.menu_items[ui_state.selected_item].data, "search") == 0) {
                        screen_search();
                    }
                } else if (ui_state.current_screen == SCREEN_BROWSE_SERIES ||
                          ui_state.current_screen == SCREEN_SEARCH) {
                    load_episodes_for_series(ui_state.menu_items[ui_state.selected_item].data);
                } else if (ui_state.current_screen == SCREEN_EPISODES) {
                    load_local_files(ui_state.menu_items[ui_state.selected_item].data);
                }
                break;
            
            case 'w':
                if (ui_state.current_screen == SCREEN_LOCAL_FILES) {
                    play_video(ui_state.menu_items[ui_state.selected_item].data);
                }
                break;
        }
    }

cleanup:
    cleanup_ui();
    db_close(db);
    return 0;
}

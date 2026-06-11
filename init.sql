-- Initialize with proper configuration
PRAGMA foreign_keys = ON;

-- jfm.db schema

-- Server configurations
CREATE TABLE IF NOT EXISTS servers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    url TEXT NOT NULL,
    api_key TEXT NOT NULL,
    enabled INTEGER DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Series/Shows
CREATE TABLE IF NOT EXISTS series (
    id TEXT PRIMARY KEY,
    server_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    jellyfin_id TEXT NOT NULL,
    last_polled DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (server_id) REFERENCES servers(id),
    UNIQUE(server_id, jellyfin_id)
);

-- Episodes
CREATE TABLE IF NOT EXISTS episodes (
    id TEXT PRIMARY KEY,
    series_id TEXT NOT NULL,
    server_id INTEGER NOT NULL,
    jellyfin_id TEXT NOT NULL,
    name TEXT NOT NULL,
    season_number INTEGER,
    episode_number INTEGER,
    runtime_ticks BIGINT,
    is_aired INTEGER DEFAULT 1,
    added_date DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (series_id) REFERENCES series(id),
    FOREIGN KEY (server_id) REFERENCES servers(id),
    UNIQUE(server_id, jellyfin_id)
);

-- Local files
CREATE TABLE IF NOT EXISTS local_files (
    id TEXT PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    series_name TEXT,
    season_number INTEGER,
    episode_number INTEGER,
    duration_ms INTEGER,
    scanned_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    indexed INTEGER DEFAULT 0
);

-- Watch history
CREATE TABLE IF NOT EXISTS watch_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id TEXT NOT NULL,
    user_name TEXT NOT NULL,
    watched_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    playback_position REAL DEFAULT 0.0,
    is_finished INTEGER DEFAULT 0,
    FOREIGN KEY (file_id) REFERENCES local_files(id),
    UNIQUE(file_id, user_name)
);

-- Series watched tracking (for Jellyfin series)
CREATE TABLE IF NOT EXISTS series_watch_state (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    episode_id TEXT NOT NULL,
    user_name TEXT NOT NULL,
    watched_at DATETIME,
    playback_position REAL DEFAULT 0.0,
    is_watched INTEGER DEFAULT 0,
    UNIQUE(episode_id, user_name),
    FOREIGN KEY (episode_id) REFERENCES episodes(id)
);

-- Exclude list
CREATE TABLE IF NOT EXISTS exclude_paths (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Search cache
CREATE TABLE IF NOT EXISTS search_cache (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    query TEXT NOT NULL,
    result_json TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    expires_at DATETIME,
    FOREIGN KEY (server_id) REFERENCES servers(id),
    UNIQUE(server_id, query)
);

-- Indices
CREATE INDEX IF NOT EXISTS idx_series_server ON series(server_id);
CREATE INDEX IF NOT EXISTS idx_episodes_series ON episodes(series_id);
CREATE INDEX IF NOT EXISTS idx_episodes_server ON episodes(server_id);
CREATE INDEX IF NOT EXISTS idx_watch_history_file ON watch_history(file_id);
CREATE INDEX IF NOT EXISTS idx_watch_history_user ON watch_history(user_name);
CREATE INDEX IF NOT EXISTS idx_local_files_series ON local_files(series_name);


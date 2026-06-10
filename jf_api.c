#include "jfm.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <cjson/cJSON.h>

typedef struct {
    char *data;
    size_t size;
} MemoryStruct;

static FILE *log_file = NULL;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Not enough memory for curl response\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

static char *_jf_api_call(const char *server_url, const char *api_key, const char *endpoint) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    MemoryStruct chunk = {0};
    chunk.data = malloc(1);
    chunk.size = 0;
    
    char url[2048];
    snprintf(url, sizeof(url), "%s/%s&api_key=%s", server_url, endpoint, api_key);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_STDERR, log_file ? log_file : stderr);

    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(res));
        free(chunk.data);
        return NULL;
    }
    
    return chunk.data;
}

char *jf_search(const char *server_url, const char *api_key, const char *query) {
    // URL encode the query
    char *encoded_query = malloc(strlen(query) * 3 + 1);
    if (!encoded_query) return NULL;
    
    char *p = encoded_query;
    for (const char *q = query; *q; q++) {
        if (*q == ' ') {
            p += sprintf(p, "%%20");
        } else if ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || 
                   (*q >= '0' && *q <= '9') || *q == '-' || *q == '_' || *q == '.') {
            *p++ = *q;
        } else {
            p += sprintf(p, "%%%02X", (unsigned char)*q);
        }
    }
    *p = 0;
    
    // Build endpoint WITHOUT leading slash and WITHOUT initial ?
    char proper_endpoint[1024];
    snprintf(proper_endpoint, sizeof(proper_endpoint), 
             "Items?searchTerm=%s&limit=50", encoded_query);
    
    free(encoded_query);
    return _jf_api_call(server_url, api_key, proper_endpoint);
}


char *jf_get_series_episodes(const char *server_url, const char *api_key, const char *series_id) {
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "Shows/%s/Episodes?userId=&limit=200", series_id);
    return _jf_api_call(server_url, api_key, endpoint);
}

char *jf_get_item(const char *server_url, const char *api_key, const char *item_id) {
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "Items/%s", item_id);
    return _jf_api_call(server_url, api_key, endpoint);
}

void json_free(char *json) {
    free(json);
}

int jf_parse_search_results(const char *json, Episode *episodes, int *count) {
    if (!json || !episodes || !count) return -1;
    
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    
    cJSON *series_searches = cJSON_GetObjectItem(root, "SearchHints");
    if (!series_searches) {
        cJSON_Delete(root);
        return -1;
    }
    
    *count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, series_searches) {
        if (*count >= 50) break;
        
        cJSON *name = cJSON_GetObjectItem(item, "Name");
        cJSON *id = cJSON_GetObjectItem(item, "ItemId");
        cJSON *type = cJSON_GetObjectItem(item, "Type");
        
        if (name && id && type && cJSON_IsString(type)) {
            strncpy(episodes[*count].name, name->valuestring, 511);
            strncpy(episodes[*count].jellyfin_id, id->valuestring, 255);
            (*count)++;
        }
    }
    
    cJSON_Delete(root);
    return 0;
}

int jf_parse_episodes(const char *json, Episode *episodes, int *count) {
    if (!json || !episodes || !count) return -1;
    
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    
    cJSON *items = cJSON_GetObjectItem(root, "Items");
    if (!items) {
        cJSON_Delete(root);
        return -1;
    }
    
    *count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (*count >= 200) break;
        
        cJSON *name = cJSON_GetObjectItem(item, "Name");
        cJSON *id = cJSON_GetObjectItem(item, "Id");
        cJSON *season = cJSON_GetObjectItem(item, "ParentIndexNumber");
        cJSON *episode = cJSON_GetObjectItem(item, "IndexNumber");
        cJSON *runtime = cJSON_GetObjectItem(item, "RunTimeTicks");
        
        if (name && id) {
            strncpy(episodes[*count].name, name->valuestring, 511);
            strncpy(episodes[*count].jellyfin_id, id->valuestring, 255);
            episodes[*count].season = season ? season->valueint : 0;
            episodes[*count].episode = episode ? episode->valueint : 0;
            // episodes[*count].runtime_ticks = runtime ? runtime->valueint64 : 0;
            episodes[*count].runtime_ticks = runtime ? runtime->valueint : 0;

            (*count)++;
        }
    }
    
    cJSON_Delete(root);
    return 0;
}

void jf_api_init_logging(const char *log_path) {
    log_file = fopen(log_path, "a");
    if (log_file) {
        fprintf(log_file, "\n=== JFM API Log Started ===\n");
        fflush(log_file);
    }
}

void jf_api_cleanup_logging(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}


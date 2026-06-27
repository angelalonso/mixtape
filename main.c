#define WEBVIEW_GTK
#define WEBVIEW_IMPLEMENTATION
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include "webview.h"
#include "server.h"

/* ------------------------------------------------------------------ */
/*  Logging                                                             */
/* ------------------------------------------------------------------ */

/* 0 = off, 1 = errors only, 2 = info, 3 = verbose */
static int g_log_level = 2;

#define LOG_ERROR(fmt, ...) do { if (g_log_level >= 1) { printf("[ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } } while(0)
#define LOG_INFO(fmt, ...)  do { if (g_log_level >= 2) { printf("[INFO]  " fmt "\n", ##__VA_ARGS__); fflush(stdout); } } while(0)
#define LOG_VERBOSE(fmt, ...) do { if (g_log_level >= 3) { printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } } while(0)

/* ------------------------------------------------------------------ */
/*  Rsync thread pool                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char cmd[8192];
    char cmd_id[128];
    char result[4096];
    int  exit_code;
    int  completed;   /* 0 = pending, 1 = claimed/running, 2 = done */
    struct webview *webview;
} rsync_task_t;

#define MAX_RSYNC_TASKS 64
static rsync_task_t rsync_tasks[MAX_RSYNC_TASKS];
static pthread_mutex_t rsync_mutex = PTHREAD_MUTEX_INITIALIZER;
static int rsync_thread_running = 1;

/* Number of parallel rsync worker threads — read from cfg at startup */
static int g_max_rsync_workers = 0;   /* 0 means: detect at runtime  */

static gboolean idle_rsync_callback(gpointer user_data);

/* Detect the number of logical CPUs available to this process. */
static int detect_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    return (int)n;
}

/*
 * Each worker thread loops, claiming one pending task at a time, executing
 * it via popen, then posting the result back to the GTK main loop through
 * g_idle_add so webview_eval is always called from the right thread.
 */
void* rsync_worker(void* arg) {
    (void)arg;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000 }; /* 50 ms */

    while (rsync_thread_running) {
        rsync_task_t *slot = NULL;
        int slot_index = -1;

        pthread_mutex_lock(&rsync_mutex);
        for (int i = 0; i < MAX_RSYNC_TASKS; i++) {
            if (rsync_tasks[i].cmd[0] != '\0' && rsync_tasks[i].completed == 0) {
                rsync_tasks[i].completed = 1; /* claim it */
                slot = &rsync_tasks[i];
                slot_index = i;
                break;
            }
        }
        pthread_mutex_unlock(&rsync_mutex);

        if (!slot) {
            nanosleep(&ts, NULL);
            continue;
        }

        /* Work on a local copy so we can release the slot immediately */
        rsync_task_t *task = (rsync_task_t *)malloc(sizeof(rsync_task_t));
        if (!task) {
            pthread_mutex_lock(&rsync_mutex);
            memset(&rsync_tasks[slot_index], 0, sizeof(rsync_task_t));
            pthread_mutex_unlock(&rsync_mutex);
            continue;
        }
        pthread_mutex_lock(&rsync_mutex);
        memcpy(task, slot, sizeof(rsync_task_t));
        memset(&rsync_tasks[slot_index], 0, sizeof(rsync_task_t));
        pthread_mutex_unlock(&rsync_mutex);

        LOG_INFO("Rsync executing [%s]: %s", task->cmd_id, task->cmd);

        FILE *fp = popen(task->cmd, "r");
        if (!fp) {
            strcpy(task->result, "Failed to execute command");
            task->exit_code = -1;
            LOG_ERROR("Rsync popen failed for task %s", task->cmd_id);
        } else {
            char line[512];
            size_t result_len = 0;
            while (fgets(line, sizeof(line), fp) != NULL) {
                size_t line_len = strlen(line);
                if (result_len + line_len < sizeof(task->result) - 1) {
                    strcpy(task->result + result_len, line);
                    result_len += line_len;
                }
                LOG_VERBOSE("rsync[%s]: %s", task->cmd_id, line);
            }
            task->exit_code = pclose(fp);
            if (WIFEXITED(task->exit_code)) {
                task->exit_code = WEXITSTATUS(task->exit_code);
            } else {
                task->exit_code = -1;
            }
        }

        LOG_INFO("Rsync task %s finished (exit %d)", task->cmd_id, task->exit_code);

        if (task->webview != NULL) {
            g_idle_add(idle_rsync_callback, task);
        } else {
            free(task);
        }
    }
    return NULL;
}

static gboolean idle_rsync_callback(gpointer user_data) {
    rsync_task_t *task = (rsync_task_t *)user_data;
    if (!task || !task->webview) {
        if (task) free(task);
        return FALSE;
    }

    char escaped_result[4096] = {0};
    js_escape(task->result, escaped_result, sizeof(escaped_result));

    char js_buf[16384];
    if (task->exit_code == 0) {
        snprintf(js_buf, sizeof(js_buf),
            "window.rsyncCallbacks && window.rsyncCallbacks['%s'] && window.rsyncCallbacks['%s']('success', '%s');",
            task->cmd_id, task->cmd_id, escaped_result);
    } else {
        snprintf(js_buf, sizeof(js_buf),
            "window.rsyncCallbacks && window.rsyncCallbacks['%s'] && window.rsyncCallbacks['%s']('error: exit code %d', '%s');",
            task->cmd_id, task->cmd_id, task->exit_code, escaped_result);
    }
    webview_eval(task->webview, js_buf);

    char notify_buf[1024];
    snprintf(notify_buf, sizeof(notify_buf),
        "console.log('[Rsync] Task %s completed with exit code %d');",
        task->cmd_id, task->exit_code);
    webview_eval(task->webview, notify_buf);

    free(task);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Config helpers                                                      */
/* ------------------------------------------------------------------ */

/* Global config values */
static int tape_check_interval_seconds = 5;

/*
 * Parse a positive integer after a YAML-style key in cfg_content.
 * Returns the parsed value if > 0, otherwise returns fallback.
 */
static int parse_int_key(const char *cfg_content, const char *key, int fallback) {
    const char *p = strstr(cfg_content, key);
    if (!p) return fallback;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    int v = atoi(p);
    return (v > 0) ? v : fallback;
}

/* ------------------------------------------------------------------ */
/*  Native callback dispatcher                                          */
/* ------------------------------------------------------------------ */

void my_external_invoke_cb(struct webview *w, const char *arg) {

    if (strcmp(arg, "exitApp") == 0) {
        webview_terminate(w);
        rsync_thread_running = 0;
    }
    else if (strcmp(arg, "getConfig") == 0 || strcmp(arg, "loadInitialData") == 0) {
        FILE *cfg_file = fopen("cfg.yml", "r");
        if (!cfg_file) {
            cfg_file = fopen("cfg.yml", "w");
            if (cfg_file) {
                fprintf(cfg_file,
                    "data_type: file\ndata_file: data.json\n"
                    "tape_check_interval: 5\nlog_level: 2\nmax_rsync_workers: 0\n");
                fclose(cfg_file);
            }
            cfg_file = fopen("cfg.yml", "r");
        }

        char cfg_content[4096] = {0};
        if (cfg_file) {
            size_t bytes = fread(cfg_content, 1, sizeof(cfg_content) - 1, cfg_file);
            cfg_content[bytes] = '\0';
            fclose(cfg_file);
        }

        char data_type[128] = {0};
        char data_file_path[256] = {0};
        parse_config(cfg_content, data_type, data_file_path);

        /* Apply runtime config values */
        tape_check_interval_seconds = parse_int_key(cfg_content, "tape_check_interval:", 5);
        g_log_level = parse_int_key(cfg_content, "log_level:", 2);
        /* clamp log_level to valid range */
        if (g_log_level < 0) g_log_level = 0;
        if (g_log_level > 3) g_log_level = 3;
        g_max_rsync_workers = parse_int_key(cfg_content, "max_rsync_workers:", 0);

        LOG_INFO("Config loaded: data_type=%s data_file=%s tape_check_interval=%d log_level=%d max_rsync_workers=%d",
            data_type, data_file_path, tape_check_interval_seconds, g_log_level, g_max_rsync_workers);

        if (strcmp(data_type, "file") != 0) {
            webview_dialog(w, WEBVIEW_DIALOG_TYPE_ALERT, WEBVIEW_DIALOG_FLAG_ERROR,
                           "Configuration Error",
                           "Invalid data_type specified in cfg.yml. Only 'file' is supported.", NULL, 0);
            webview_eval(w, "window.initializeData('', '', 1);");
            return;
        }

        FILE *d_file = fopen(data_file_path, "r");
        if (!d_file) {
            d_file = fopen(data_file_path, "w");
            if (d_file) {
                fprintf(d_file, "{\"mixes\":[],\"tapes\":[],\"mixTapes\":[]}");
                fclose(d_file);
            }
            d_file = fopen(data_file_path, "r");
        }

        char data_content[8192] = {0};
        if (d_file) {
            size_t bytes = fread(data_content, 1, sizeof(data_content) - 1, d_file);
            data_content[bytes] = '\0';
            fclose(d_file);
        }

        char *escaped_cfg  = malloc(8192);
        char *escaped_data = malloc(16384);
        js_escape(cfg_content,  escaped_cfg,  8192);
        js_escape(data_content, escaped_data, 16384);

        char *js_eval = malloc(32768);
        snprintf(js_eval, 32768, "window.initializeData(\"%s\", \"%s\", 0);",
                 escaped_cfg, escaped_data);
        webview_eval(w, js_eval);

        char interval_js[256];
        snprintf(interval_js, sizeof(interval_js),
            "window.receiveTapeCheckInterval(%d);", tape_check_interval_seconds);
        webview_eval(w, interval_js);

        free(escaped_cfg);
        free(escaped_data);
        free(js_eval);
    }
    else if (strcmp(arg, "getTapeCheckInterval") == 0) {
        char interval_js[256];
        snprintf(interval_js, sizeof(interval_js),
            "window.receiveTapeCheckInterval(%d);", tape_check_interval_seconds);
        webview_eval(w, interval_js);
    }
    else if (strncmp(arg, "saveData:", 9) == 0) {
        const char *json_str = arg + 9;
        FILE *cfg_file = fopen("cfg.yml", "r");

        char cfg_content[4096] = {0};
        if (cfg_file) {
            size_t bytes = fread(cfg_content, 1, sizeof(cfg_content) - 1, cfg_file);
            cfg_content[bytes] = '\0';
            fclose(cfg_file);
        } else {
            strcpy(cfg_content, "data_type: file\ndata_file: data.json\n");
        }

        char data_type[128]     = {0};
        char data_file_path[256] = {0};
        parse_config(cfg_content, data_type, data_file_path);

        if (strcmp(data_type, "file") == 0) {
            FILE *d_file = fopen(data_file_path, "w");
            if (d_file) {
                fprintf(d_file, "%s", json_str);
                fclose(d_file);
                LOG_VERBOSE("Data saved to %s", data_file_path);
            } else {
                LOG_ERROR("Failed to open data file for writing: %s", data_file_path);
            }
        }
    }
    else if (strcmp(arg, "selectTapeFolder") == 0) {
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Select Tape Directory Storage Folder",
            GTK_WINDOW(w->priv.window),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Open",   GTK_RESPONSE_ACCEPT,
            NULL);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (path != NULL) {
                char js_buf[4120];
                char escaped[2048] = {0};
                js_escape(path, escaped, sizeof(escaped));
                snprintf(js_buf, sizeof(js_buf), "window.receiveSelectedFolder(\"%s\");", escaped);
                webview_eval(w, js_buf);
                LOG_VERBOSE("Tape folder selected: %s", path);
                g_free(path);
            }
        }
        gtk_widget_destroy(dialog);
    }
    else if (strcmp(arg, "selectMixPaths") == 0) {
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Select Mix Files",
            GTK_WINDOW(w->priv.window),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Add",    GTK_RESPONSE_ACCEPT,
            NULL);

        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            GSList *paths = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));

            size_t buf_size = 32768;
            char *js_buf = malloc(buf_size);
            strcpy(js_buf, "window.receiveMixPaths([");

            GSList *iter = paths;
            while (iter != NULL) {
                char *path = (char *)iter->data;
                char escaped[2048] = {0};
                js_escape(path, escaped, sizeof(escaped));
                strcat(js_buf, "\"");
                strcat(js_buf, escaped);
                strcat(js_buf, "\"");
                if (iter->next != NULL) strcat(js_buf, ", ");
                g_free(path);
                iter = iter->next;
            }
            g_slist_free(paths);
            strcat(js_buf, "]);");

            webview_eval(w, js_buf);
            free(js_buf);
        }
        gtk_widget_destroy(dialog);
    }
    else if (strcmp(arg, "selectMixFolders") == 0) {
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Select Mix Folders",
            GTK_WINDOW(w->priv.window),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Add",    GTK_RESPONSE_ACCEPT,
            NULL);

        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            GSList *paths = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));

            size_t buf_size = 32768;
            char *js_buf = malloc(buf_size);
            strcpy(js_buf, "window.receiveMixFolders([");

            GSList *iter = paths;
            while (iter != NULL) {
                char *path = (char *)iter->data;
                char escaped[2048] = {0};
                js_escape(path, escaped, sizeof(escaped));
                strcat(js_buf, "\"");
                strcat(js_buf, escaped);
                strcat(js_buf, "\"");
                if (iter->next != NULL) strcat(js_buf, ", ");
                g_free(path);
                iter = iter->next;
            }
            g_slist_free(paths);
            strcat(js_buf, "]);");

            webview_eval(w, js_buf);
            free(js_buf);
        }
        gtk_widget_destroy(dialog);
    }
    else if (strncmp(arg, "rsync:", 6) == 0) {
        const char *cmd_part = arg + 6;
        char cmd[8192]  = {0};
        char cmd_id[128] = {0};

        const char *pipe_pos = strchr(cmd_part, '|');
        if (!pipe_pos) {
            strncpy(cmd, cmd_part, sizeof(cmd) - 1);
            snprintf(cmd_id, sizeof(cmd_id), "rsync-%d", (int)time(NULL));
        } else {
            size_t cmd_len = (size_t)(pipe_pos - cmd_part);
            if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
            strncpy(cmd, cmd_part, cmd_len);
            cmd[cmd_len] = '\0';
            strncpy(cmd_id, pipe_pos + 1, sizeof(cmd_id) - 1);
        }

        char full_cmd[16384];
        snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);

        pthread_mutex_lock(&rsync_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_RSYNC_TASKS; i++) {
            if (rsync_tasks[i].cmd[0] == '\0') { slot = i; break; }
        }

        if (slot == -1) {
            char js_buf[1024];
            snprintf(js_buf, sizeof(js_buf),
                "window.rsyncCallbacks && window.rsyncCallbacks['%s'] && window.rsyncCallbacks['%s']('error: task queue full', '');",
                cmd_id, cmd_id);
            webview_eval(w, js_buf);
            pthread_mutex_unlock(&rsync_mutex);
            LOG_ERROR("Rsync task queue full, rejected task %s", cmd_id);
            return;
        }

        memset(&rsync_tasks[slot], 0, sizeof(rsync_task_t));
        strncpy(rsync_tasks[slot].cmd,    full_cmd, sizeof(rsync_tasks[slot].cmd) - 1);
        strncpy(rsync_tasks[slot].cmd_id, cmd_id,   sizeof(rsync_tasks[slot].cmd_id) - 1);
        rsync_tasks[slot].webview   = w;
        rsync_tasks[slot].completed = 0;
        pthread_mutex_unlock(&rsync_mutex);

        LOG_INFO("Rsync task queued [%s]: %s", cmd_id, cmd);
    }
    else if (strncmp(arg, "createMarker:", 13) == 0) {
        const char *path = arg + 13;
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "MixTape tape marker\n");
            fclose(f);
            LOG_INFO("Marker created: %s", path);
        } else {
            LOG_ERROR("Failed to create marker: %s", path);
        }
    }
    else if (strncmp(arg, "checkTapeAvailability:", 22) == 0) {
        const char *path = arg + 22;
        char marker_path[4096];
        snprintf(marker_path, sizeof(marker_path), "%s/.mixtape", path);

        struct stat st;
        int available = (stat(marker_path, &st) == 0);

        char js_buf[8192];
        char escaped_path[4096];
        js_escape(path, escaped_path, sizeof(escaped_path));

        snprintf(js_buf, sizeof(js_buf),
            "window.receiveTapeAvailability({\"path\":\"%s\",\"available\":%s});",
            escaped_path, available ? "true" : "false");
        webview_eval(w, js_buf);

        LOG_VERBOSE("Tape availability: %s = %s", path, available ? "true" : "false");
    }
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void) {
    struct webview webview = {
        .title    = "MixTape",
        .width    = 1280,
        .height   = 960,
        .resizable = 1,
        .debug    = 1,
        .external_invoke_cb = my_external_invoke_cb
    };

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "Error: Unable to get current working directory.\n");
        return 1;
    }

    char url[4150];
    snprintf(url, sizeof(url), "file://%s/index.html", cwd);
    webview.url = url;

    int res = webview_init(&webview);
    if (res != 0) {
        fprintf(stderr, "Error: WebView initialization failed with code %d. "
                        "Ensure you have a running X11/Wayland display server.\n", res);
        return 1;
    }

    /*
     * Determine worker count.  g_max_rsync_workers is 0 until the config is
     * loaded by the first JS->C call, so we read it from the file directly
     * here as well so the thread pool is sized correctly from the start.
     */
    {
        FILE *cf = fopen("cfg.yml", "r");
        if (cf) {
            char tmp[4096] = {0};
            size_t b = fread(tmp, 1, sizeof(tmp) - 1, cf);
            tmp[b] = '\0';
            fclose(cf);
            g_log_level        = parse_int_key(tmp, "log_level:", 2);
            if (g_log_level < 0) g_log_level = 0;
            if (g_log_level > 3) g_log_level = 3;
            g_max_rsync_workers = parse_int_key(tmp, "max_rsync_workers:", 0);
        }
    }

    int worker_count = g_max_rsync_workers;
    if (worker_count <= 0) {
        worker_count = detect_cpu_count();
    }
    /* Cap to half the task slots so the queue never fully saturates */
    if (worker_count > MAX_RSYNC_TASKS / 2) worker_count = MAX_RSYNC_TASKS / 2;

    LOG_INFO("Starting %d rsync worker thread(s) (CPUs detected: %d)",
             worker_count, detect_cpu_count());

    pthread_t *rsync_threads = calloc((size_t)worker_count, sizeof(pthread_t));
    if (!rsync_threads) {
        fprintf(stderr, "Error: Failed to allocate thread array.\n");
        return 1;
    }

    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&rsync_threads[i], NULL, rsync_worker, NULL) != 0) {
            fprintf(stderr, "Warning: Failed to create rsync worker thread %d.\n", i);
        } else {
            pthread_detach(rsync_threads[i]);
            LOG_VERBOSE("Rsync worker thread %d started.", i);
        }
    }

    while (webview_loop(&webview, 1) == 0) { }

    rsync_thread_running = 0;

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000000 };
    nanosleep(&ts, NULL);

    webview_exit(&webview);
    free(rsync_threads);
    return 0;
}

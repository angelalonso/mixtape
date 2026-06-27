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

typedef struct {
    char cmd[8192];
    char cmd_id[128];
    char result[4096];
    int exit_code;
    int completed;
    struct webview *webview;
} rsync_task_t;

#define MAX_RSYNC_TASKS 16
rsync_task_t rsync_tasks[MAX_RSYNC_TASKS];
int rsync_task_count = 0;
pthread_mutex_t rsync_mutex = PTHREAD_MUTEX_INITIALIZER;
int rsync_thread_running = 1;

static gboolean idle_rsync_callback(gpointer user_data);

void* rsync_worker(void* arg) {
    (void)arg;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;
    
    while (rsync_thread_running) {
        rsync_task_t task;
        int has_task = 0;
        
        pthread_mutex_lock(&rsync_mutex);
        for (int i = 0; i < MAX_RSYNC_TASKS; i++) {
            if (rsync_tasks[i].completed == 0 && rsync_tasks[i].cmd[0] != '\0') {
                task = rsync_tasks[i];
                rsync_tasks[i].completed = 1;
                has_task = 1;
                break;
            }
        }
        pthread_mutex_unlock(&rsync_mutex);
        
        if (!has_task) {
            nanosleep(&ts, NULL);
            continue;
        }
        
        printf("[Rsync] Executing: %s\n", task.cmd);
        fflush(stdout);
        
        FILE *fp = popen(task.cmd, "r");
        if (fp == NULL) {
            strcpy(task.result, "Failed to execute command");
            task.exit_code = -1;
        } else {
            char line[512];
            size_t result_len = 0;
            while (fgets(line, sizeof(line), fp) != NULL) {
                size_t line_len = strlen(line);
                if (result_len + line_len < sizeof(task.result) - 1) {
                    strcpy(task.result + result_len, line);
                    result_len += line_len;
                }
                printf("%s", line);
                fflush(stdout);
            }
            task.exit_code = pclose(fp);
            if (WIFEXITED(task.exit_code)) {
                task.exit_code = WEXITSTATUS(task.exit_code);
            } else {
                task.exit_code = -1;
            }
        }
        
        printf("[Rsync] Task %s completed with exit code %d\n", task.cmd_id, task.exit_code);
        fflush(stdout);
        
        if (task.webview != NULL) {
            rsync_task_t *task_copy = (rsync_task_t*)malloc(sizeof(rsync_task_t));
            if (task_copy != NULL) {
                memcpy(task_copy, &task, sizeof(rsync_task_t));
                g_idle_add(idle_rsync_callback, task_copy);
            }
        }
        
        pthread_mutex_lock(&rsync_mutex);
        memset(&task, 0, sizeof(task));
        pthread_mutex_unlock(&rsync_mutex);
    }
    return NULL;
}

static gboolean idle_rsync_callback(gpointer user_data) {
    rsync_task_t *task = (rsync_task_t*)user_data;
    if (task == NULL || task->webview == NULL) {
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

// Global config values
int tape_check_interval_seconds = 5;

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
                fprintf(cfg_file, "data_type: file\ndata_file: data.json\n");
                fprintf(cfg_file, "tape_check_interval: 5\n");
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
        
        // Parse tape_check_interval from config
        const char* interval_str = strstr(cfg_content, "tape_check_interval:");
        if (interval_str) {
            interval_str += 20; // Skip "tape_check_interval:"
            while (*interval_str == ' ' || *interval_str == '\t') interval_str++;
            int val = atoi(interval_str);
            if (val > 0) {
                tape_check_interval_seconds = val;
                printf("[Config] Tape check interval set to %d seconds\n", tape_check_interval_seconds);
                fflush(stdout);
            }
        }
        
        if (strcmp(data_type, "file") != 0) {
            webview_dialog(w, WEBVIEW_DIALOG_TYPE_ALERT, WEBVIEW_DIALOG_FLAG_ERROR, 
                           "Configuration Error", "Invalid data_type specified in cfg.yml. Only 'file' is supported.", NULL, 0);
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
        
        char *escaped_cfg = malloc(8192);
        char *escaped_data = malloc(16384);
        js_escape(cfg_content, escaped_cfg, 8192);
        js_escape(data_content, escaped_data, 16384);
        
        char *js_eval = malloc(32768);
        snprintf(js_eval, 32768, "window.initializeData(\"%s\", \"%s\", 0);", escaped_cfg, escaped_data);
        webview_eval(w, js_eval);
        
        // Send tape check interval to JS
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
        
        char data_type[128] = {0};
        char data_file_path[256] = {0};
        parse_config(cfg_content, data_type, data_file_path);
        
        if (strcmp(data_type, "file") == 0) {
            FILE *d_file = fopen(data_file_path, "w");
            if (d_file) {
                fprintf(d_file, "%s", json_str);
                fclose(d_file);
            }
        }
    }
    else if (strcmp(arg, "selectTapeFolder") == 0) {
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Select Tape Directory Storage Folder",
            GTK_WINDOW(w->priv.window),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Open", GTK_RESPONSE_ACCEPT,
            NULL
        );

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (path != NULL) {
                char js_buf[4120];
                char escaped[2048] = {0};
                js_escape(path, escaped, sizeof(escaped));
                snprintf(js_buf, sizeof(js_buf), "window.receiveSelectedFolder(\"%s\");", escaped);
                webview_eval(w, js_buf);
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
            "_Add", GTK_RESPONSE_ACCEPT,
            NULL
        );
        
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
                
                if (iter->next != NULL) {
                    strcat(js_buf, ", ");
                }
                
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
            "_Add", GTK_RESPONSE_ACCEPT,
            NULL
        );
        
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
                
                if (iter->next != NULL) {
                    strcat(js_buf, ", ");
                }
                
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
        char cmd[8192] = {0};
        char cmd_id[128] = {0};
        
        const char *pipe_pos = strchr(cmd_part, '|');
        if (pipe_pos == NULL) {
            strncpy(cmd, cmd_part, sizeof(cmd) - 1);
            cmd[sizeof(cmd) - 1] = '\0';
            snprintf(cmd_id, sizeof(cmd_id), "rsync-%d", (int)time(NULL));
        } else {
            size_t cmd_len = pipe_pos - cmd_part;
            if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
            strncpy(cmd, cmd_part, cmd_len);
            cmd[cmd_len] = '\0';
            strncpy(cmd_id, pipe_pos + 1, sizeof(cmd_id) - 1);
            cmd_id[sizeof(cmd_id) - 1] = '\0';
        }
        
        char full_cmd[16384];
        snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);
        
        pthread_mutex_lock(&rsync_mutex);
        int task_index = -1;
        for (int i = 0; i < MAX_RSYNC_TASKS; i++) {
            if (rsync_tasks[i].cmd[0] == '\0') {
                task_index = i;
                break;
            }
        }
        
        if (task_index == -1) {
            char js_buf[1024];
            snprintf(js_buf, sizeof(js_buf), 
                "window.rsyncCallbacks && window.rsyncCallbacks['%s'] && window.rsyncCallbacks['%s']('error: too many rsync tasks', '');",
                cmd_id, cmd_id);
            webview_eval(w, js_buf);
            pthread_mutex_unlock(&rsync_mutex);
            return;
        }
        
        memset(&rsync_tasks[task_index], 0, sizeof(rsync_task_t));
        strncpy(rsync_tasks[task_index].cmd, full_cmd, sizeof(rsync_tasks[task_index].cmd) - 1);
        rsync_tasks[task_index].cmd[sizeof(rsync_tasks[task_index].cmd) - 1] = '\0';
        strncpy(rsync_tasks[task_index].cmd_id, cmd_id, sizeof(rsync_tasks[task_index].cmd_id) - 1);
        rsync_tasks[task_index].cmd_id[sizeof(rsync_tasks[task_index].cmd_id) - 1] = '\0';
        rsync_tasks[task_index].webview = w;
        rsync_tasks[task_index].completed = 0;
        pthread_mutex_unlock(&rsync_mutex);
        
        printf("[Rsync] Started background task %s: %s\n", cmd_id, cmd);
        fflush(stdout);
    }
    else if (strncmp(arg, "createMarker:", 13) == 0) {
        const char *path = arg + 13;
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "MixTape tape marker\n");
            fclose(f);
            printf("[Marker] Created: %s\n", path);
        } else {
            printf("[Marker] Failed to create: %s\n", path);
        }
        fflush(stdout);
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
        
        // printf("[Availability] Path: %s, Available: %s\n", path, available ? "true" : "false");
        fflush(stdout);
    }
}

int main() {
    struct webview webview = {
        .title = "MixTape",
        .width = 1280,
        .height = 960,
        .resizable = 1,
        .debug = 1,
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
        fprintf(stderr, "Error: WebView initialization failed with code %d. Ensure you have a running X11/Wayland display server.\n", res);
        return 1;
    }

    pthread_t rsync_thread;
    if (pthread_create(&rsync_thread, NULL, rsync_worker, NULL) != 0) {
        fprintf(stderr, "Warning: Failed to create rsync background thread. Rsync operations will block.\n");
    } else {
        pthread_detach(rsync_thread);
        fprintf(stderr, "Rsync background worker thread started.\n");
    }

    while (webview_loop(&webview, 1) == 0) {
    }

    rsync_thread_running = 0;
    
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;
    nanosleep(&ts, NULL);
    
    webview_exit(&webview);
    return 0;
}

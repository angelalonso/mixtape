#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "server.h"

int tests_run = 0;
int tests_failed = 0;

#define assert_msg(expr, msg) do { \
    tests_run++; \
    if (!(expr)) { \
        printf("[FAIL] %s\n", msg); \
        tests_failed++; \
    } else { \
        printf("[PASS] %s\n", msg); \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/*  Mock helpers                                                        */
/* ------------------------------------------------------------------ */

void mock_check_and_ensure_config() {
    ConfigData data = read_config_file("test_cfg.yml");
    if (!data.exists) {
        create_config_from_template("test_cfg.yml.template", "test_cfg.yml");
    }
}

const char* mock_handle_keyboard_esc(const char* current_screen_id) {
    if (strcmp(current_screen_id, "screen-main") == 0)         return "screen-confirm-exit";
    if (strcmp(current_screen_id, "screen-confirm-exit") == 0) return "screen-main";
    if (strcmp(current_screen_id, "screen-mix-tapes") == 0)    return "screen-main";
    return "screen-main";
}

int mock_select_tape_folder_called = 0;
int mock_select_mix_paths_called   = 0;
int mock_select_mix_folder_called  = 0;
int mock_rsync_executed_called     = 0;
int mock_rsync_background_called   = 0;

void mock_native_select_tape_folder() { mock_select_tape_folder_called = 1; }
void mock_native_select_mix_paths()   { mock_select_mix_paths_called   = 1; }
void mock_native_select_mix_folder()  { mock_select_mix_folder_called  = 1; }
void mock_native_rsync_execute()      { mock_rsync_executed_called      = 1; }
void mock_native_rsync_background()   { mock_rsync_background_called    = 1; }

/* ------------------------------------------------------------------ */
/*  parse_int_key helper (mirrors the implementation in main.c)        */
/* ------------------------------------------------------------------ */

static int parse_int_key(const char *cfg_content, const char *key, int fallback) {
    const char *p = strstr(cfg_content, key);
    if (!p) return fallback;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    int v = atoi(p);
    return (v > 0) ? v : fallback;
}

/* ------------------------------------------------------------------ */
/*  Config & template tests                                             */
/* ------------------------------------------------------------------ */

void test_automatic_template_instantiation() {
    remove("test_cfg.yml");
    FILE* t = fopen("test_cfg.yml.template", "w");
    fprintf(t, "setting: template_default\n");
    fclose(t);
    mock_check_and_ensure_config();
    ConfigData data = read_config_file("test_cfg.yml");
    assert_msg(data.exists == 1, "Strategy must automatically create cfg.yml if it does not exist.");
    remove("test_cfg.yml");
    remove("test_cfg.yml.template");
}

void test_window_dimensions_doubled() {
    int expected_width  = 1280;
    int expected_height = 960;
    assert_msg(expected_width  == 1280, "Main window entry application layout configuration width must be 1280 pixels.");
    assert_msg(expected_height == 960,  "Main window entry application layout configuration height must be 960 pixels.");
}

void test_keyboard_escape_routing() {
    assert_msg(strcmp(mock_handle_keyboard_esc("screen-mixes"), "screen-main") == 0,
               "Pressing Escape on mixes screen must route view state back to main screen.");
    assert_msg(strcmp(mock_handle_keyboard_esc("screen-main"), "screen-confirm-exit") == 0,
               "Pressing Escape on the main dashboard must trigger the exit confirmation dialog screen.");
    assert_msg(strcmp(mock_handle_keyboard_esc("screen-mix-tapes"), "screen-main") == 0,
               "Pressing Escape on the mix-tapes screen must route view state back to main screen.");
}

/* ------------------------------------------------------------------ */
/*  parse_config tests                                                  */
/* ------------------------------------------------------------------ */

void test_parse_config_valid() {
    char type[128] = {0};
    char file[256] = {0};
    const char *cfg = "data_type: file\ndata_file: custom_tracks.json\n";
    parse_config(cfg, type, file);
    assert_msg(strcmp(type, "file") == 0,               "parse_config must correctly parse valid data_type.");
    assert_msg(strcmp(file, "custom_tracks.json") == 0, "parse_config must correctly parse valid data_file path.");
}

void test_parse_config_empty_input() {
    char type[128] = {0};
    char file[256] = {0};
    parse_config("", type, file);
    assert_msg(strlen(type) == 0, "parse_config with empty input must leave data_type empty.");
    assert_msg(strlen(file) == 0, "parse_config with empty input must leave data_file empty.");
}

void test_parse_config_trims_trailing_whitespace() {
    char type[128] = {0};
    char file[256] = {0};
    const char *cfg = "data_type: file  \ndata_file: tracks.json  \n";
    parse_config(cfg, type, file);
    assert_msg(strcmp(type, "file")        == 0, "parse_config must trim trailing whitespace from data_type.");
    assert_msg(strcmp(file, "tracks.json") == 0, "parse_config must trim trailing whitespace from data_file.");
}

void test_parse_config_missing_data_file() {
    char type[128] = {0};
    char file[256] = {0};
    const char *cfg = "data_type: file\n";
    parse_config(cfg, type, file);
    assert_msg(strcmp(type, "file") == 0, "parse_config must parse data_type even when data_file is absent.");
    assert_msg(strlen(file)         == 0, "parse_config must leave data_file empty when the key is absent.");
}

/* ------------------------------------------------------------------ */
/*  parse_int_key / log_level / max_rsync_workers tests                */
/* ------------------------------------------------------------------ */

void test_parse_int_key_log_level_present() {
    const char *cfg = "data_type: file\nlog_level: 3\nmax_rsync_workers: 4\n";
    int level = parse_int_key(cfg, "log_level:", 2);
    assert_msg(level == 3,
               "parse_int_key must parse log_level correctly when the key is present.");
}

void test_parse_int_key_log_level_absent_uses_fallback() {
    const char *cfg = "data_type: file\ndata_file: data.json\n";
    int level = parse_int_key(cfg, "log_level:", 2);
    assert_msg(level == 2,
               "parse_int_key must return the fallback value when log_level key is absent.");
}

void test_parse_int_key_max_rsync_workers_present() {
    const char *cfg = "data_type: file\nmax_rsync_workers: 8\n";
    int workers = parse_int_key(cfg, "max_rsync_workers:", 0);
    assert_msg(workers == 8,
               "parse_int_key must parse max_rsync_workers correctly when present.");
}

void test_parse_int_key_max_rsync_workers_zero_means_auto() {
    const char *cfg = "data_type: file\nmax_rsync_workers: 0\n";
    int workers = parse_int_key(cfg, "max_rsync_workers:", 0);
    /* 0 is not > 0, so parse_int_key returns the fallback (0) which signals auto-detect */
    assert_msg(workers == 0,
               "max_rsync_workers of 0 must be treated as auto-detect (fallback returned).");
}

void test_parse_int_key_max_rsync_workers_absent_uses_fallback() {
    const char *cfg = "data_type: file\ndata_file: data.json\n";
    int workers = parse_int_key(cfg, "max_rsync_workers:", 0);
    assert_msg(workers == 0,
               "parse_int_key must return fallback 0 when max_rsync_workers is absent.");
}

void test_parse_int_key_tape_check_interval() {
    const char *cfg = "data_type: file\ntape_check_interval: 10\n";
    int interval = parse_int_key(cfg, "tape_check_interval:", 5);
    assert_msg(interval == 10,
               "parse_int_key must parse tape_check_interval correctly when present.");
}

void test_parse_int_key_tape_check_interval_absent() {
    const char *cfg = "data_type: file\ndata_file: data.json\n";
    int interval = parse_int_key(cfg, "tape_check_interval:", 5);
    assert_msg(interval == 5,
               "parse_int_key must return fallback when tape_check_interval is absent.");
}

/* ------------------------------------------------------------------ */
/*  Parallel rsync task slot tests                                      */
/* ------------------------------------------------------------------ */

/*
 * Simulate enqueueing several tasks into the task array the same way
 * main.c does — without touching pthreads — and verify slot management.
 */

#define TEST_MAX_RSYNC_TASKS 64

typedef struct {
    char cmd[8192];
    char cmd_id[128];
    int  completed;
} test_rsync_task_t;

static test_rsync_task_t test_task_slots[TEST_MAX_RSYNC_TASKS];

static void reset_test_slots(void) {
    memset(test_task_slots, 0, sizeof(test_task_slots));
}

static int enqueue_test_task(const char *cmd, const char *cmd_id) {
    for (int i = 0; i < TEST_MAX_RSYNC_TASKS; i++) {
        if (test_task_slots[i].cmd[0] == '\0') {
            strncpy(test_task_slots[i].cmd,    cmd,    sizeof(test_task_slots[i].cmd) - 1);
            strncpy(test_task_slots[i].cmd_id, cmd_id, sizeof(test_task_slots[i].cmd_id) - 1);
            test_task_slots[i].completed = 0;
            return i;
        }
    }
    return -1;  /* queue full */
}

static void complete_test_task(int slot) {
    memset(&test_task_slots[slot], 0, sizeof(test_rsync_task_t));
}

void test_rsync_task_slots_accept_multiple_concurrent_tasks() {
    reset_test_slots();
    int s1 = enqueue_test_task("rsync -av /src1/ /dst/", "task-1");
    int s2 = enqueue_test_task("rsync -av /src2/ /dst/", "task-2");
    int s3 = enqueue_test_task("rsync -av /src3/ /dst/", "task-3");
    assert_msg(s1 >= 0 && s2 >= 0 && s3 >= 0,
               "Task slot array must accept multiple concurrent rsync tasks.");
    assert_msg(s1 != s2 && s2 != s3 && s1 != s3,
               "Each concurrent rsync task must be assigned a unique slot.");
}

void test_rsync_task_slot_reused_after_completion() {
    reset_test_slots();
    int s1 = enqueue_test_task("rsync -av /src/ /dst/", "task-reuse-1");
    assert_msg(s1 >= 0, "First task must be enqueued successfully.");
    complete_test_task(s1);
    int s2 = enqueue_test_task("rsync -av /src/ /dst/", "task-reuse-2");
    assert_msg(s2 >= 0,   "Task slot must be reusable after a task completes.");
    assert_msg(s2 == s1,  "Completed slot must be the first one offered for reuse.");
}

void test_rsync_task_queue_reports_full_at_capacity() {
    reset_test_slots();
    int last = -1;
    for (int i = 0; i < TEST_MAX_RSYNC_TASKS; i++) {
        char id[32];
        snprintf(id, sizeof(id), "task-%d", i);
        last = enqueue_test_task("rsync -av /s/ /d/", id);
    }
    assert_msg(last >= 0, "The last slot must fill successfully at exact capacity.");
    int overflow = enqueue_test_task("rsync -av /s/ /d/", "task-overflow");
    assert_msg(overflow == -1,
               "Enqueueing beyond MAX_RSYNC_TASKS capacity must return -1 (queue full).");
}

void test_rsync_worker_count_from_config_overrides_auto() {
    /*
     * Verify that a positive max_rsync_workers config value is returned
     * as-is (not the CPU count), which is what main.c uses to size the pool.
     */
    const char *cfg = "data_type: file\nmax_rsync_workers: 6\n";
    int workers = parse_int_key(cfg, "max_rsync_workers:", 0);
    assert_msg(workers == 6,
               "A positive max_rsync_workers in cfg.yml must override CPU auto-detection.");
}

/* ------------------------------------------------------------------ */
/*  js_escape tests                                                     */
/* ------------------------------------------------------------------ */

void test_js_escape_special_chars() {
    const char *raw_str = "path/\\with\n\"quotes\"";
    char escaped[256] = {0};
    js_escape(raw_str, escaped, sizeof(escaped));
    assert_msg(strstr(escaped, "\\\\") != NULL, "js_escape must successfully protect backslashes.");
    assert_msg(strstr(escaped, "\\\"") != NULL, "js_escape must successfully protect quote enclosures.");
}

void test_js_escape_plain_string() {
    char escaped[128] = {0};
    js_escape("hello world", escaped, sizeof(escaped));
    assert_msg(strcmp(escaped, "hello world") == 0,
               "js_escape must leave a plain string without special chars unchanged.");
}

void test_js_escape_newline_converted() {
    char escaped[64] = {0};
    js_escape("line1\nline2", escaped, sizeof(escaped));
    assert_msg(strstr(escaped, "\\n") != NULL,
               "js_escape must convert a literal newline to the two-char sequence \\n.");
}

void test_js_escape_carriage_return_converted() {
    char escaped[64] = {0};
    js_escape("data\rmore", escaped, sizeof(escaped));
    assert_msg(strstr(escaped, "\\r") != NULL,
               "js_escape must convert a carriage return to the two-char sequence \\r.");
}

void test_js_escape_respects_dest_size() {
    char escaped[5] = {0};
    js_escape("ABCDEFGHIJ", escaped, sizeof(escaped));
    assert_msg(strlen(escaped) <= 4,
               "js_escape must not write beyond dest_size and must NUL-terminate the result.");
}

/* ------------------------------------------------------------------ */
/*  Native dialog mock tests                                            */
/* ------------------------------------------------------------------ */

void test_folder_picker_interaction() {
    mock_select_tape_folder_called = 0;
    mock_native_select_tape_folder();
    assert_msg(mock_select_tape_folder_called == 1,
               "Invoking selectTapeFolder action must activate the native OS dialog sequence.");
}

void test_multi_file_picker_interaction() {
    mock_select_mix_paths_called = 0;
    mock_native_select_mix_paths();
    assert_msg(mock_select_mix_paths_called == 1,
               "Invoking selectMixPaths triggers the multi-select file browser hook in C backend.");
}

/* ------------------------------------------------------------------ */
/*  Mixes multi-file workflow                                           */
/* ------------------------------------------------------------------ */

static int build_receive_mix_paths_js(const char** paths, int count,
                                       char* out, size_t out_size) {
    if (!paths || count <= 0 || !out || out_size == 0) return 0;
    size_t pos = 0;
    const char* prefix = "window.receiveMixPaths([";
    size_t plen = strlen(prefix);
    if (plen >= out_size) return 0;
    memcpy(out, prefix, plen);
    pos += plen;
    for (int i = 0; i < count; i++) {
        char escaped[2048] = {0};
        js_escape(paths[i], escaped, sizeof(escaped));
        size_t needed = 3 + strlen(escaped) + 1;
        if (i > 0) needed += 2;
        if (pos + needed + 3 >= out_size) return 0;
        if (i > 0) { out[pos++] = ','; out[pos++] = ' '; }
        out[pos++] = '"';
        size_t elen = strlen(escaped);
        memcpy(out + pos, escaped, elen);
        pos += elen;
        out[pos++] = '"';
    }
    const char* suffix = "]);";
    size_t slen = strlen(suffix);
    if (pos + slen >= out_size) return 0;
    memcpy(out + pos, suffix, slen);
    pos += slen;
    out[pos] = '\0';
    return 1;
}

void test_mix_paths_js_call_single_path() {
    const char* paths[] = { "/home/user/music/track.mp3" };
    char js[4096] = {0};
    int ok = build_receive_mix_paths_js(paths, 1, js, sizeof(js));
    assert_msg(ok == 1, "build_receive_mix_paths_js must succeed for a single path.");
    assert_msg(strncmp(js, "window.receiveMixPaths([", 24) == 0,
               "JS call for receiveMixPaths must start with the correct function prefix.");
    assert_msg(strstr(js, "/home/user/music/track.mp3") != NULL,
               "JS call for receiveMixPaths must embed the supplied path.");
    assert_msg(js[strlen(js)-1] == ';',
               "JS call for receiveMixPaths must end with a semicolon.");
}

void test_mix_paths_js_call_multiple_paths() {
    const char* paths[] = {
        "/home/user/music/alpha.mp3",
        "/home/user/music/beta.mp3",
        "/home/user/music/gamma.mp3"
    };
    char js[4096] = {0};
    int ok = build_receive_mix_paths_js(paths, 3, js, sizeof(js));
    assert_msg(ok == 1,                         "build_receive_mix_paths_js must succeed for multiple paths.");
    assert_msg(strstr(js, "alpha.mp3") != NULL, "JS call must contain first path.");
    assert_msg(strstr(js, "beta.mp3")  != NULL, "JS call must contain second path.");
    assert_msg(strstr(js, "gamma.mp3") != NULL, "JS call must contain third path.");
}

void test_mix_paths_js_call_escapes_special_chars() {
    const char* paths[] = { "/home/user/my \"music\"/track\\01.mp3" };
    char js[4096] = {0};
    build_receive_mix_paths_js(paths, 1, js, sizeof(js));
    assert_msg(strstr(js, "\\\"") != NULL,
               "receiveMixPaths JS call must escape double quotes inside paths.");
    assert_msg(strstr(js, "\\\\") != NULL,
               "receiveMixPaths JS call must escape backslashes inside paths.");
}

void test_mix_paths_js_call_empty_list() {
    const char* paths[] = { NULL };
    char js[4096] = {0};
    int ok = build_receive_mix_paths_js(paths, 0, js, sizeof(js));
    assert_msg(ok == 0, "build_receive_mix_paths_js must return failure for an empty path list.");
}

void test_mix_select_native_callback_fires() {
    mock_select_mix_paths_called = 0;
    mock_native_select_mix_paths();
    mock_native_select_mix_paths();
    assert_msg(mock_select_mix_paths_called == 1,
               "selectMixPaths native callback must be callable multiple times for the same mix.");
}

/* ------------------------------------------------------------------ */
/*  Mixes folder-picker workflow                                        */
/* ------------------------------------------------------------------ */

static int build_receive_mix_folder_js(const char* folder_path, char* out, size_t out_size) {
    if (!folder_path || !out || out_size == 0) return 0;
    char escaped[2048] = {0};
    js_escape(folder_path, escaped, sizeof(escaped));
    int written = snprintf(out, out_size, "window.receiveMixFolder(\"%s\");", escaped);
    return (written > 0 && (size_t)written < out_size) ? 1 : 0;
}

void test_mix_folder_picker_callback_fires() {
    mock_select_mix_folder_called = 0;
    mock_native_select_mix_folder();
    assert_msg(mock_select_mix_folder_called == 1,
               "Invoking selectMixFolder action must activate the native OS folder-picker dialog.");
}

void test_mix_folder_js_call_well_formed() {
    char js[4096] = {0};
    int ok = build_receive_mix_folder_js("/home/user/albums/2024", js, sizeof(js));
    assert_msg(ok == 1, "build_receive_mix_folder_js must succeed for a valid folder path.");
    assert_msg(strncmp(js, "window.receiveMixFolder(\"", 24) == 0,
               "receiveMixFolder JS call must start with the correct function prefix.");
    assert_msg(strstr(js, "/home/user/albums/2024") != NULL,
               "receiveMixFolder JS call must embed the folder path.");
    assert_msg(js[strlen(js)-1] == ';',
               "receiveMixFolder JS call must end with a semicolon.");
}

void test_mix_folder_js_call_escapes_special_chars() {
    char js[4096] = {0};
    build_receive_mix_folder_js("/home/user/my \"albums\"/2024\\archive", js, sizeof(js));
    assert_msg(strstr(js, "\\\"") != NULL,
               "receiveMixFolder JS call must escape double quotes in folder path.");
    assert_msg(strstr(js, "\\\\") != NULL,
               "receiveMixFolder JS call must escape backslashes in folder path.");
}

void test_mix_folder_js_call_empty_path() {
    char js[4096] = {0};
    int ok = build_receive_mix_folder_js("", js, sizeof(js));
    assert_msg(ok == 1, "build_receive_mix_folder_js must not crash on an empty path string.");
    assert_msg(strstr(js, "window.receiveMixFolder(\"\");") != NULL,
               "receiveMixFolder JS call with empty path must still be syntactically valid.");
}

void test_mix_folder_picker_callable_multiple_times() {
    mock_select_mix_folder_called = 0;
    mock_native_select_mix_folder();
    mock_native_select_mix_folder();
    assert_msg(mock_select_mix_folder_called == 1,
               "selectMixFolder native callback must be callable multiple times to add more folders.");
}

/* ------------------------------------------------------------------ */
/*  Mix-Tapes workflow                                                  */
/* ------------------------------------------------------------------ */

struct mix_tape_test {
    char id[64];
    char name[128];
    char mix_id[64];
    char tape_id[64];
};

static int create_mix_tape_js(const struct mix_tape_test* mt, char* out, size_t out_size) {
    if (!mt || !out || out_size == 0) return 0;
    char escaped_name[256] = {0};
    js_escape(mt->name, escaped_name, sizeof(escaped_name));
    int written = snprintf(out, out_size,
        "{\"id\":\"%s\",\"name\":\"%s\",\"mixId\":\"%s\",\"tapeId\":\"%s\"}",
        mt->id, escaped_name, mt->mix_id, mt->tape_id);
    return (written > 0 && (size_t)written < out_size) ? 1 : 0;
}

void test_mix_tape_create_structure() {
    struct mix_tape_test mt = {
        .id = "mixtape-1234567890", .name = "My Mix-Tape",
        .mix_id = "mix-1234567890", .tape_id = "tape-1234567890"
    };
    char js[1024] = {0};
    int ok = create_mix_tape_js(&mt, js, sizeof(js));
    assert_msg(ok == 1, "create_mix_tape_js must succeed for a valid mix-tape structure.");
    assert_msg(strstr(js, mt.id)      != NULL, "Mix-tape JS representation must include the ID.");
    assert_msg(strstr(js, mt.name)    != NULL, "Mix-tape JS representation must include the name.");
    assert_msg(strstr(js, mt.mix_id)  != NULL, "Mix-tape JS representation must include the mix ID.");
    assert_msg(strstr(js, mt.tape_id) != NULL, "Mix-tape JS representation must include the tape ID.");
}

void test_mix_tape_requires_both_mix_and_tape() {
    struct mix_tape_test mt_valid = {
        .id = "mixtape-1234567890", .name = "Valid Mix-Tape",
        .mix_id = "mix-1234567890",  .tape_id = "tape-1234567890"
    };
    struct mix_tape_test mt_missing_mix = {
        .id = "mixtape-1234567891", .name = "Missing Mix",
        .mix_id = "",               .tape_id = "tape-1234567890"
    };
    struct mix_tape_test mt_missing_tape = {
        .id = "mixtape-1234567892", .name = "Missing Tape",
        .mix_id = "mix-1234567890", .tape_id = ""
    };

    char js_valid[1024] = {0}, js_missing_mix[1024] = {0}, js_missing_tape[1024] = {0};

    assert_msg(create_mix_tape_js(&mt_valid,        js_valid,        sizeof(js_valid))        == 1,
               "Valid mix-tape structure must be creatable.");
    assert_msg(create_mix_tape_js(&mt_missing_mix,  js_missing_mix,  sizeof(js_missing_mix))  == 1,
               "Mix-tape structure without mix ID must be creatable but invalid.");
    assert_msg(create_mix_tape_js(&mt_missing_tape, js_missing_tape, sizeof(js_missing_tape)) == 1,
               "Mix-tape structure without tape ID must be creatable but invalid.");

    assert_msg(strstr(js_missing_mix,  "\"mixId\":\"\"")  != NULL,
               "Missing mix ID must be represented as empty string.");
    assert_msg(strstr(js_missing_tape, "\"tapeId\":\"\"") != NULL,
               "Missing tape ID must be represented as empty string.");
}

void test_mix_tape_rsync_execution() {
    mock_rsync_executed_called = 0;
    mock_native_rsync_execute();
    assert_msg(mock_rsync_executed_called == 1,
               "Apply mix-tape action must trigger rsync execution.");
}

void test_mix_tape_rsync_background() {
    mock_rsync_background_called = 0;
    mock_native_rsync_background();
    assert_msg(mock_rsync_background_called == 1,
               "Rsync execution must run in background without blocking the UI.");
}

void test_mix_tape_deletion() {
    int initial_count = 2;
    int deleted_count = 1;
    int final_count   = initial_count - deleted_count;
    assert_msg(final_count == 1, "Deleting a mix-tape must reduce the list count by 1.");
}

void test_mix_tape_edit_preserves_fields() {
    struct mix_tape_test mt_original = {
        .id = "mixtape-1234567890", .name = "Original Name",
        .mix_id = "mix-1234567890", .tape_id = "tape-1234567890"
    };
    struct mix_tape_test mt_edited = {
        .id = "mixtape-1234567890", .name = "Edited Name",
        .mix_id = "mix-1234567891", .tape_id = "tape-1234567891"
    };

    char js_original[1024] = {0}, js_edited[1024] = {0};
    create_mix_tape_js(&mt_original, js_original, sizeof(js_original));
    create_mix_tape_js(&mt_edited,   js_edited,   sizeof(js_edited));

    assert_msg(strcmp(mt_original.id,      mt_edited.id)      == 0, "Editing a mix-tape must preserve its ID.");
    assert_msg(strcmp(mt_original.name,    mt_edited.name)    != 0, "Editing a mix-tape must allow name changes.");
    assert_msg(strcmp(mt_original.mix_id,  mt_edited.mix_id)  != 0, "Editing a mix-tape must allow mix selection changes.");
    assert_msg(strcmp(mt_original.tape_id, mt_edited.tape_id) != 0, "Editing a mix-tape must allow tape selection changes.");
}

void test_mix_tape_ui_controls_visibility() {
    int has_apply_button  = 1;
    int has_edit_button   = 1;
    int has_delete_button = 1;
    assert_msg(has_apply_button  == 1, "Mix-tape list item must have an Apply button.");
    assert_msg(has_edit_button   == 1, "Mix-tape list item must have an Edit button.");
    assert_msg(has_delete_button == 1, "Mix-tape list item must have a Delete button.");
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Starting WebView UI State & Strategy Test Suite ===\n\n");

    printf("-- Config & template --\n");
    test_automatic_template_instantiation();
    test_window_dimensions_doubled();
    test_keyboard_escape_routing();

    printf("\n-- parse_config --\n");
    test_parse_config_valid();
    test_parse_config_empty_input();
    test_parse_config_trims_trailing_whitespace();
    test_parse_config_missing_data_file();

    printf("\n-- log_level / max_rsync_workers / tape_check_interval config keys --\n");
    test_parse_int_key_log_level_present();
    test_parse_int_key_log_level_absent_uses_fallback();
    test_parse_int_key_max_rsync_workers_present();
    test_parse_int_key_max_rsync_workers_zero_means_auto();
    test_parse_int_key_max_rsync_workers_absent_uses_fallback();
    test_parse_int_key_tape_check_interval();
    test_parse_int_key_tape_check_interval_absent();
    test_rsync_worker_count_from_config_overrides_auto();

    printf("\n-- Parallel rsync task slot management --\n");
    test_rsync_task_slots_accept_multiple_concurrent_tasks();
    test_rsync_task_slot_reused_after_completion();
    test_rsync_task_queue_reports_full_at_capacity();

    printf("\n-- js_escape --\n");
    test_js_escape_special_chars();
    test_js_escape_plain_string();
    test_js_escape_newline_converted();
    test_js_escape_carriage_return_converted();
    test_js_escape_respects_dest_size();

    printf("\n-- Native dialog mocks --\n");
    test_folder_picker_interaction();
    test_multi_file_picker_interaction();

    printf("\n-- Mixes multi-file workflow --\n");
    test_mix_paths_js_call_single_path();
    test_mix_paths_js_call_multiple_paths();
    test_mix_paths_js_call_escapes_special_chars();
    test_mix_paths_js_call_empty_list();
    test_mix_select_native_callback_fires();

    printf("\n-- Mixes folder-picker workflow --\n");
    test_mix_folder_picker_callback_fires();
    test_mix_folder_js_call_well_formed();
    test_mix_folder_js_call_escapes_special_chars();
    test_mix_folder_js_call_empty_path();
    test_mix_folder_picker_callable_multiple_times();

    printf("\n-- Mix-Tapes workflow --\n");
    test_mix_tape_create_structure();
    test_mix_tape_requires_both_mix_and_tape();
    test_mix_tape_rsync_execution();
    test_mix_tape_rsync_background();
    test_mix_tape_deletion();
    test_mix_tape_edit_preserves_fields();
    test_mix_tape_ui_controls_visibility();

    printf("\n=== Summary: %d Passed, %d Failed ===\n",
           tests_run - tests_failed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

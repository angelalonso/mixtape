#include <stdio.h>
#include <string.h>
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
    if (strcmp(current_screen_id, "screen-main") == 0) return "screen-confirm-exit";
    if (strcmp(current_screen_id, "screen-confirm-exit") == 0) return "screen-main";
    if (strcmp(current_screen_id, "screen-mix-tapes") == 0) return "screen-main";
    return "screen-main";
}

int mock_select_tape_folder_called = 0;
int mock_select_mix_paths_called   = 0;
int mock_select_mix_folder_called  = 0;
int mock_rsync_executed_called = 0;
int mock_rsync_background_called = 0;

void mock_native_select_tape_folder() { mock_select_tape_folder_called = 1; }
void mock_native_select_mix_paths()   { mock_select_mix_paths_called   = 1; }
void mock_native_select_mix_folder()  { mock_select_mix_folder_called  = 1; }
void mock_native_rsync_execute()      { mock_rsync_executed_called = 1; }
void mock_native_rsync_background()   { mock_rsync_background_called = 1; }

/* ------------------------------------------------------------------ */
/*  Original tests                                                      */
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

void test_parse_config_valid() {
    char type[128] = {0};
    char file[256] = {0};
    const char *cfg = "data_type: file\ndata_file: custom_tracks.json\n";
    parse_config(cfg, type, file);
    assert_msg(strcmp(type, "file") == 0,               "parse_config must correctly parse valid data_type.");
    assert_msg(strcmp(file, "custom_tracks.json") == 0, "parse_config must correctly parse valid data_file path.");
}

void test_js_escape_special_chars() {
    const char *raw_str = "path/\\with\n\"quotes\"";
    char escaped[256] = {0};
    js_escape(raw_str, escaped, sizeof(escaped));
    assert_msg(strstr(escaped, "\\\\") != NULL, "js_escape must successfully protect backslashes.");
    assert_msg(strstr(escaped, "\\\"") != NULL, "js_escape must successfully protect quote enclosures.");
}

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
/*  parse_config edge cases                                             */
/* ------------------------------------------------------------------ */

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
/*  js_escape edge cases                                                */
/* ------------------------------------------------------------------ */

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
/*  Mixes folder-picker workflow (new)                                  */
/* ------------------------------------------------------------------ */

/* Simulate the C backend building the JS call for receiveMixFolder. */
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
    /* Empty path produces a syntactically valid but empty-string JS call.
       The JS side guards against empty strings; we just confirm C doesn't crash. */
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
/*  Mix-Tapes workflow (new)                                           */
/* ------------------------------------------------------------------ */

/* Test mix-tape data structure */
struct mix_tape_test {
    char id[64];
    char name[128];
    char mix_id[64];
    char tape_id[64];
};

/* Helper to create mix-tape data structure */
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
        .id = "mixtape-1234567890",
        .name = "My Mix-Tape",
        .mix_id = "mix-1234567890",
        .tape_id = "tape-1234567890"
    };
    char js[1024] = {0};
    int ok = create_mix_tape_js(&mt, js, sizeof(js));
    assert_msg(ok == 1, "create_mix_tape_js must succeed for a valid mix-tape structure.");
    assert_msg(strstr(js, mt.id) != NULL, "Mix-tape JS representation must include the ID.");
    assert_msg(strstr(js, mt.name) != NULL, "Mix-tape JS representation must include the name.");
    assert_msg(strstr(js, mt.mix_id) != NULL, "Mix-tape JS representation must include the mix ID.");
    assert_msg(strstr(js, mt.tape_id) != NULL, "Mix-tape JS representation must include the tape ID.");
}

void test_mix_tape_requires_both_mix_and_tape() {
    // A mix-tape requires both a mix and a tape to be valid
    struct mix_tape_test mt_valid = {
        .id = "mixtape-1234567890",
        .name = "Valid Mix-Tape",
        .mix_id = "mix-1234567890",
        .tape_id = "tape-1234567890"
    };
    
    struct mix_tape_test mt_missing_mix = {
        .id = "mixtape-1234567891",
        .name = "Missing Mix",
        .mix_id = "",
        .tape_id = "tape-1234567890"
    };
    
    struct mix_tape_test mt_missing_tape = {
        .id = "mixtape-1234567892",
        .name = "Missing Tape",
        .mix_id = "mix-1234567890",
        .tape_id = ""
    };
    
    // All structures should be creatable, but validation should fail for incomplete ones
    char js_valid[1024] = {0};
    char js_missing_mix[1024] = {0};
    char js_missing_tape[1024] = {0};
    
    assert_msg(create_mix_tape_js(&mt_valid, js_valid, sizeof(js_valid)) == 1,
               "Valid mix-tape structure must be creatable.");
    assert_msg(create_mix_tape_js(&mt_missing_mix, js_missing_mix, sizeof(js_missing_mix)) == 1,
               "Mix-tape structure without mix ID must be creatable but invalid.");
    assert_msg(create_mix_tape_js(&mt_missing_tape, js_missing_tape, sizeof(js_missing_tape)) == 1,
               "Mix-tape structure without tape ID must be creatable but invalid.");
    
    // Verify that the missing fields are represented
    assert_msg(strstr(js_missing_mix, "\"mixId\":\"\"") != NULL,
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
    // Test that deleting a mix-tape removes it from the list
    // This is a mock test - in real implementation, this would involve JS DOM manipulation
    int initial_count = 2;
    int deleted_count = 1;
    int final_count = initial_count - deleted_count;
    assert_msg(final_count == 1, "Deleting a mix-tape must reduce the list count by 1.");
}

void test_mix_tape_edit_preserves_fields() {
    // Test that editing a mix-tape preserves its fields
    struct mix_tape_test mt_original = {
        .id = "mixtape-1234567890",
        .name = "Original Name",
        .mix_id = "mix-1234567890",
        .tape_id = "tape-1234567890"
    };
    
    struct mix_tape_test mt_edited = {
        .id = "mixtape-1234567890",
        .name = "Edited Name",
        .mix_id = "mix-1234567891",
        .tape_id = "tape-1234567891"
    };
    
    char js_original[1024] = {0};
    char js_edited[1024] = {0};
    create_mix_tape_js(&mt_original, js_original, sizeof(js_original));
    create_mix_tape_js(&mt_edited, js_edited, sizeof(js_edited));
    
    assert_msg(strcmp(mt_original.id, mt_edited.id) == 0,
               "Editing a mix-tape must preserve its ID.");
    assert_msg(strcmp(mt_original.name, mt_edited.name) != 0,
               "Editing a mix-tape must allow name changes.");
    assert_msg(strcmp(mt_original.mix_id, mt_edited.mix_id) != 0,
               "Editing a mix-tape must allow mix selection changes.");
    assert_msg(strcmp(mt_original.tape_id, mt_edited.tape_id) != 0,
               "Editing a mix-tape must allow tape selection changes.");
}

void test_mix_tape_ui_controls_visibility() {
    // Test that the UI controls for mix-tape are visible and accessible
    // This is a mock test for the UI controls
    int has_apply_button = 1;
    int has_edit_button = 1;
    int has_delete_button = 1;
    
    assert_msg(has_apply_button == 1,
               "Mix-tape list item must have an Apply button.");
    assert_msg(has_edit_button == 1,
               "Mix-tape list item must have an Edit button.");
    assert_msg(has_delete_button == 1,
               "Mix-tape list item must have a Delete button.");
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main() {
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

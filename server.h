#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int exists;
    char content[4096];
} ConfigData;

/* Read a config file into a ConfigData struct. exists=0 if file not found. */
static inline ConfigData read_config_file(const char* filename) {
    ConfigData data = {0, ""};
    FILE* file = fopen(filename, "r");
    if (!file) {
        return data;
    }
    data.exists = 1;
    size_t bytes = fread(data.content, 1, sizeof(data.content) - 1, file);
    data.content[bytes] = '\0';
    fclose(file);
    return data;
}

/* Copy a template file to a destination file verbatim. Returns 1 on success. */
static inline int create_config_from_template(const char* template_fn, const char* dest_fn) {
    FILE* src = fopen(template_fn, "r");
    if (!src) return 0;

    FILE* dest = fopen(dest_fn, "w");
    if (!dest) {
        fclose(src);
        return 0;
    }

    char ch;
    while ((ch = fgetc(src)) != EOF) {
        fputc(ch, dest);
    }

    fclose(src);
    fclose(dest);
    return 1;
}

/*
 * Parse a YAML-style config string for two keys:
 *   data_type: <value>
 *   data_file: <value>
 * Results are written into the caller-supplied buffers.
 */
static inline void parse_config(const char* cfg, char* data_type, char* data_file) {
    if (!cfg || !data_type || !data_file) return;

    const char* p = cfg;
    while (*p) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "data_type:", 10) == 0) {
            p += 10;
            while (*p == ' ' || *p == '\t') p++;
            int i = 0;
            while (*p && *p != '\n' && *p != '\r' && i < 127) {
                data_type[i++] = *p++;
            }
            data_type[i] = '\0';
            /* Trim trailing whitespace */
            while (i > 0 && (data_type[i-1] == ' ' || data_type[i-1] == '\t')) {
                data_type[--i] = '\0';
            }
        } else if (strncmp(p, "data_file:", 10) == 0) {
            p += 10;
            while (*p == ' ' || *p == '\t') p++;
            int i = 0;
            while (*p && *p != '\n' && *p != '\r' && i < 255) {
                data_file[i++] = *p++;
            }
            data_file[i] = '\0';
            while (i > 0 && (data_file[i-1] == ' ' || data_file[i-1] == '\t')) {
                data_file[--i] = '\0';
            }
        }

        /* Advance to the next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/*
 * Escape a raw string for safe embedding inside a JavaScript double-quoted
 * string literal.  Escapes: \ -> \\, " -> \", newline -> \n, carriage
 * return -> \r.  Output is always NUL-terminated and will not exceed
 * dest_size bytes (including the NUL terminator).
 */
static inline void js_escape(const char* src, char* dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) return;
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 2 < dest_size; i++) {
        char c = src[i];
        if (c == '\\') {
            dest[j++] = '\\'; dest[j++] = '\\';
        } else if (c == '"') {
            dest[j++] = '\\'; dest[j++] = '"';
        } else if (c == '\n') {
            dest[j++] = '\\'; dest[j++] = 'n';
        } else if (c == '\r') {
            dest[j++] = '\\'; dest[j++] = 'r';
        } else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

#endif /* SERVER_H */

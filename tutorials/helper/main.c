#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    char *title;
    char *body;
} help_section_t;

typedef struct {
    help_section_t *items;
    size_t count;
} help_doc_t;

static struct termios g_orig_termios;
static bool g_raw_enabled = false;

static void disable_raw_mode(void) {
    if (g_raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_enabled = false;
    }
}

static void enable_raw_mode(void) {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) return;
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return;
    g_raw_enabled = true;
    atexit(disable_raw_mode);
}

static void free_help_doc(help_doc_t *doc) {
    if (!doc || !doc->items) return;
    for (size_t i = 0; i < doc->count; ++i) {
        free(doc->items[i].title);
        free(doc->items[i].body);
    }
    free(doc->items);
    doc->items = NULL;
    doc->count = 0;
}

static char *trim_newline(char *s) {
    if (!s) return s;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static char *dup_string(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *copy = malloc(len);
    if (!copy) return NULL;
    memcpy(copy, src, len);
    return copy;
}

static bool append_text(char **buf, size_t *cap, size_t *len, const char *src) {
    if (!buf || !cap || !len || !src) return false;
    size_t need = strlen(src);
    if (*len + need + 1 >= *cap) {
        size_t new_cap = (*cap == 0) ? 256 : *cap * 2;
        while (new_cap <= *len + need + 1) new_cap *= 2;
        char *tmp = realloc(*buf, new_cap);
        if (!tmp) return false;
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, src, need);
    *len += need;
    (*buf)[*len] = '\0';
    return true;
}

static bool add_section(help_doc_t *doc, char *title, char *body) {
    if (!doc || !title || !body) return false;
    help_section_t *tmp = realloc(doc->items, (doc->count + 1) * sizeof(help_section_t));
    if (!tmp) return false;
    doc->items = tmp;
    doc->items[doc->count].title = title;
    doc->items[doc->count].body = body;
    doc->count++;
    return true;
}

static bool load_help_file(const char *path, help_doc_t *doc) {
    if (!path || !doc) return false;
    memset(doc, 0, sizeof(*doc));
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("fopen");
        return false;
    }
    char line[1024];
    char *current_title = NULL;
    char *current_body = NULL;
    size_t body_cap = 0;
    size_t body_len = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "## ", 3) == 0) {
            if (current_title) {
                if (!add_section(doc, current_title, current_body ? current_body : dup_string(""))) {
                    fclose(fp);
                    return false;
                }
                current_title = NULL;
                current_body = NULL;
                body_cap = body_len = 0;
            }
            current_title = dup_string(trim_newline(line + 3));
            if (!current_title) {
                fclose(fp);
                return false;
            }
            continue;
        }
        if (current_title) {
            if (!current_body) {
                current_body = malloc(1);
                if (!current_body) {
                    fclose(fp);
                    return false;
                }
                current_body[0] = '\0';
                body_cap = 1;
                body_len = 0;
            }
            if (!append_text(&current_body, &body_cap, &body_len, line)) {
                fclose(fp);
                return false;
            }
        }
    }

    if (current_title) {
        if (!add_section(doc, current_title, current_body ? current_body : dup_string(""))) {
            fclose(fp);
            return false;
        }
    }

    fclose(fp);
    return doc->count > 0;
}

static void save_marked_section(const char *title, const char *body) {
    if (!title || !body) return;
    FILE *fp = fopen("marked_explanations.txt", "a");
    if (!fp) {
        perror("fopen");
        return;
    }
    fprintf(fp, "[%s]\n%s\n---\n", title, body);
    fclose(fp);
    printf("Saved selection to marked_explanations.txt\n");
}

static void render_section(const help_doc_t *doc, size_t index) {
    if (!doc || index >= doc->count) return;
    const help_section_t *sec = &doc->items[index];
    printf("\033[2J\033[H"); // clear screen
    printf("[%zu/%zu] %s\n\n", index + 1, doc->count, sec->title);
    printf("%s\n", sec->body ? sec->body : "(no details)");
    printf("\nControls: i↑  k↓  Enter=mark  Backspace/Esc=exit\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "./libttak.hlp";
    help_doc_t doc;
    if (!load_help_file(path, &doc)) {
        fprintf(stderr, "Failed to load help file: %s\n", path);
        return 1;
    }

    enable_raw_mode();
    size_t index = 0;
    render_section(&doc, index);

    int ch;
    while ((ch = getchar()) != EOF) {
        if (ch == 'i' || ch == 'I') {
            if (index > 0) {
                index--;
                render_section(&doc, index);
            }
        } else if (ch == 'k' || ch == 'K') {
            if (index + 1 < doc.count) {
                index++;
                render_section(&doc, index);
            }
        } else if (ch == '\r' || ch == '\n') {
            save_marked_section(doc.items[index].title, doc.items[index].body);
            render_section(&doc, index);
        } else if (ch == 27 || ch == 8 || ch == 127) {
            break;
        } else {
            continue;
        }
    }

    disable_raw_mode();
    free_help_doc(&doc);
    printf("\nGoodbye!\n");
    return 0;
}

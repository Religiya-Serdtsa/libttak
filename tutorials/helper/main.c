#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <strings.h>

#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>

typedef struct {
    char *title;
    char *body;
} help_section_t;

typedef struct {
    help_section_t *items;
    size_t count;
} help_doc_t;

typedef struct {
    char *key;
    char *title;
    char *readme_path;
    char *code_path;
    char *body;
    bool readme_missing;
} tutorial_entry_t;

typedef struct {
    tutorial_entry_t *items;
    size_t count;
} tutorial_index_t;

static struct termios g_orig_termios;
static bool g_raw_enabled = false;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

static void ttak_mem_free_help_doc(help_doc_t *doc) {
    if (!doc || !doc->items) return;
    for (size_t i = 0; i < doc->count; ++i) {
        ttak_mem_free(doc->items[i].title);
        ttak_mem_free(doc->items[i].body);
    }
    ttak_mem_free(doc->items);
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

static char *trim_spaces(char *s) {
    if (!s) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static char *dup_string(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *copy = ttak_mem_alloc(len, __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count_ns());
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
        char *tmp = ttak_mem_realloc(*buf, new_cap, __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count_ns());
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
    help_section_t *tmp =
        ttak_mem_realloc(doc->items, (doc->count + 1) * sizeof(help_section_t), __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count_ns());
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
                if (!add_section(doc, current_title,
                                 current_body ? current_body : dup_string(""))) {
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
                current_body = ttak_mem_alloc(1, __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count_ns());
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
        if (!add_section(doc, current_title,
                         current_body ? current_body : dup_string(""))) {
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
    printf("\033[2J\033[H");
    printf("[%zu/%zu] %s\n\n", index + 1, doc->count, sec->title);
    printf("%s\n", sec->body ? sec->body : "(no details)");
    printf("\nControls: i↑  k↓  Enter=mark  Backspace/Esc=exit\n");
    fflush(stdout);
}

static void ttak_mem_free_tutorial_index(tutorial_index_t *index) {
    if (!index) return;
    for (size_t i = 0; i < index->count; ++i) {
        ttak_mem_free(index->items[i].key);
        ttak_mem_free(index->items[i].title);
        ttak_mem_free(index->items[i].readme_path);
        ttak_mem_free(index->items[i].code_path);
        ttak_mem_free(index->items[i].body);
    }
    ttak_mem_free(index->items);
    index->items = NULL;
    index->count = 0;
}

static char *read_entire_file(const char *path) {
    if (!path) return NULL;
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = ttak_mem_alloc((size_t)len + 1, __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count_ns());
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)len, fp);
    buf[read] = '\0';
    fclose(fp);
    return buf;
}

static char *derive_readme_title(const char *path, const char *fallback) {
    FILE *fp = path ? fopen(path, "r") : NULL;
    if (!fp) {
        return fallback ? dup_string(fallback) : NULL;
    }
    char line[2048];
    char *result = NULL;
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        char *trimmed = trim_spaces(line);
        if (!trimmed || *trimmed == '\0') continue;
        while (*trimmed == '#') trimmed++;
        trimmed = trim_spaces(trimmed);
        if (trimmed && *trimmed) {
            result = dup_string(trimmed);
            break;
        }
    }
    fclose(fp);
    if (!result && fallback) result = dup_string(fallback);
    return result;
}

static bool append_tutorial_entry(tutorial_index_t *index,
                                  const char *key,
                                  const char *title,
                                  const char *path,
                                  const char *code_path,
                                  bool missing) {
    if (!index || !key || !title) return false;
    tutorial_entry_t *tmp =
        ttak_mem_realloc(index->items, (index->count + 1) * sizeof(*index->items), __TTAK_UNSAFE_MEM_FOREVER__, ttak_get_tick_count_ns());
    if (!tmp) return false;
    index->items = tmp;
    tutorial_entry_t *entry = &index->items[index->count];
    entry->key = dup_string(key);
    entry->title = dup_string(title);
    entry->readme_path = path ? dup_string(path) : NULL;
    entry->code_path = code_path ? dup_string(code_path) : NULL;
    entry->body = NULL;
    entry->readme_missing = missing;
    index->count++;
    return true;
}

static int lesson_filter(const struct dirent *ent) {
    if (!ent) return 0;
    if (ent->d_name[0] == '.') return 0;
    if (!isdigit((unsigned char)ent->d_name[0])) return 0;
    return 1;
}

static bool ensure_readme_loaded(tutorial_entry_t *entry) {
    if (!entry) return false;
    if (entry->body) return true;
    if (!entry->readme_path) {
        entry->body = dup_string("(no README assigned)\n");
        return entry->body != NULL;
    }
    char *content = read_entire_file(entry->readme_path);
    if (!content) {
        entry->body = dup_string("README not found for this lesson.\n");
        entry->readme_missing = true;
        return entry->body != NULL;
    }
    entry->body = content;
    entry->readme_missing = false;
    return true;
}

static bool file_missing(const char *path) {
    struct stat st;
    return stat(path, &st) != 0;
}

static bool has_suffix_icase(const char *name, const char *suffix) {
    if (!name || !suffix) return false;
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > name_len) return false;
    return strncasecmp(name + name_len - suffix_len, suffix, suffix_len) == 0;
}

static int code_file_score(const char *name) {
    if (!name) return INT_MAX;
    static const char *const exts[] = {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hpp",
        ".s",
        ".S",
        ".asm",
        ".rs",
        ".go",
        ".py",
        ".js",
        ".ts",
        ".java",
        ".swift"
    };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (has_suffix_icase(name, exts[i])) {
            int lesson_bias = strstr(name, "lesson") ? 0 : 1;
            return (int)(i * 10) + lesson_bias;
        }
    }
    return INT_MAX;
}

static char *find_primary_code_in_dir(const char *dir_path) {
    if (!dir_path) return NULL;
    DIR *dir = opendir(dir_path);
    if (!dir) return NULL;
    int best_score = INT_MAX;
    char best_path[PATH_MAX] = {0};
    char best_name[PATH_MAX] = {0};
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (!ent || ent->d_name[0] == '.') continue;
        char file_path[PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(file_path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        int score = code_file_score(ent->d_name);
        if (score == INT_MAX) continue;
        if (score < best_score ||
            (score == best_score && strcmp(ent->d_name, best_name) < 0)) {
            best_score = score;
            strncpy(best_path, file_path, sizeof(best_path));
            best_path[sizeof(best_path) - 1] = '\0';
            strncpy(best_name, ent->d_name, sizeof(best_name));
            best_name[sizeof(best_name) - 1] = '\0';
        }
    }
    closedir(dir);
    if (best_score == INT_MAX) return NULL;
    return dup_string(best_path);
}

static bool build_tutorial_index(const char *tutorial_root,
                                 tutorial_index_t *index) {
    if (!tutorial_root || !index) return false;
    memset(index, 0, sizeof(*index));

    char path_buf[PATH_MAX];
    snprintf(path_buf, sizeof(path_buf), "%s/CLONE_PATH.md", tutorial_root);
    char *title = derive_readme_title(path_buf, "Clone Path");
    if (title) {
        if (!append_tutorial_entry(index,
                                   "CLONE_PATH",
                                   title,
                                   path_buf,
                                   NULL,
                                   file_missing(path_buf))) {
            ttak_mem_free(title);
            return false;
        }
        ttak_mem_free(title);
    }

    snprintf(path_buf, sizeof(path_buf), "%s/README.md", tutorial_root);
    title = derive_readme_title(path_buf, "Tutorial Overview");
    if (title) {
        if (!append_tutorial_entry(index,
                                   "README",
                                   title,
                                   path_buf,
                                   NULL,
                                   file_missing(path_buf))) {
            ttak_mem_free(title);
            return false;
        }
        ttak_mem_free(title);
    }

    snprintf(path_buf, sizeof(path_buf), "%s/DANGEROUS/README.md",
             tutorial_root);
    title = derive_readme_title(path_buf, "Dangerous Overview");
    if (title) {
        if (!append_tutorial_entry(index,
                                   "DANGEROUS",
                                   title,
                                   path_buf,
                                   NULL,
                                   file_missing(path_buf))) {
            ttak_mem_free(title);
            return false;
        }
        ttak_mem_free(title);
    }

    struct dirent **list = NULL;
    int count = scandir(tutorial_root, &list, lesson_filter, alphasort);
    if (count < 0) {
        perror("scandir");
        return index->count > 0;
    }

    for (int i = 0; i < count; ++i) {
        char dir_path[PATH_MAX - 11];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", tutorial_root,
                 list[i]->d_name);
        struct stat st;
        if (stat(dir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            // scandir is not a ttak function.
            // this is included in dirent.h
            free(list[i]);
            continue;
        }
        snprintf(path_buf, sizeof(path_buf), "%s/README.md", dir_path);
        bool missing = file_missing(path_buf);
        title = derive_readme_title(path_buf, list[i]->d_name);
        if (!title) title = dup_string(list[i]->d_name);
        char *code_path = find_primary_code_in_dir(dir_path);
        if (!append_tutorial_entry(index,
                                   list[i]->d_name,
                                   title,
                                   path_buf,
                                   code_path,
                                   missing)) {
            ttak_mem_free(title);
            ttak_mem_free(code_path);
            // scandir is not a ttak function.
            // this is included in dirent.h
            free(list[i]);
            continue;
        }
        ttak_mem_free(title);
        ttak_mem_free(code_path);
        // scandir is not a ttak function.
        // this is included in dirent.h
        free(list[i]);
    }
    // scandir is not a ttak function.
    // this is included in dirent.h
    free(list);
    return index->count > 0;
}

static void render_toc(const tutorial_index_t *index, size_t selected) {
    printf("\033[2J\033[H");
    printf("Tutorial Table of Contents (%zu entries)\n\n", index->count);
    for (size_t i = 0; i < index->count; ++i) {
        const tutorial_entry_t *entry = &index->items[i];
        printf("%c [%2zu] %-24s — %s%s\n",
               (i == selected) ? '>' : ' ',
               i + 1,
               entry->key ? entry->key : "",
               entry->title ? entry->title : "(untitled)",
               entry->readme_missing ? "  (README pending)" : "");
    }
    printf("\nControls: i↑  k↓  Enter=open README  Esc=exit\n");
    fflush(stdout);
}

static void render_readme(const tutorial_index_t *index, size_t idx) {
    if (!index || idx >= index->count) return;
    const tutorial_entry_t *entry = &index->items[idx];
    printf("\033[2J\033[H");
    printf("[%zu/%zu] %s\nPath: %s\n\n",
           idx + 1,
           index->count,
           entry->title ? entry->title : "(untitled)",
           entry->readme_path ? entry->readme_path : "(n/a)");
    printf("%s\n",
           entry->body ? entry->body
                       : "(Use Enter at the table of contents to load this README)");
    printf("\nControls: i↑ prev  k↓ next  Enter=README pager  Tab=code pager (Tab again=TOC)  Backspace=TOC  Esc=exit\n");
    fflush(stdout);
}

static bool run_less_on_path(const char *path) {
    if (!path) return false;
    disable_raw_mode();
    pid_t pid = fork();
    if (pid == 0) {
        execlp("less", "less", "-R", path, (char *)NULL);
        perror("less");
        _exit(1);
    } else if (pid > 0) {
        int status;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
    } else {
        perror("fork");
        enable_raw_mode();
        return false;
    }
    enable_raw_mode();
    return true;
}

static bool show_entry_readme_in_less(const tutorial_entry_t *entry) {
    if (!entry) return false;
    if (!entry->readme_path) {
        printf("\nNo README path for %s\n", entry->key ? entry->key : "?");
        fflush(stdout);
        return false;
    }
    if (entry->readme_missing) {
        printf("\nREADME missing for %s\n", entry->key ? entry->key : "?");
        fflush(stdout);
        return false;
    }
    return run_less_on_path(entry->readme_path);
}

static bool show_entry_code_in_less(const tutorial_entry_t *entry) {
    if (!entry) return false;
    if (!entry->code_path) {
        printf("\nNo code sample found for %s\n", entry->key ? entry->key : "?");
        fflush(stdout);
        return false;
    }
    struct stat st;
    if (stat(entry->code_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        printf("\nUnable to open code for %s\n", entry->key ? entry->key : "?");
        fflush(stdout);
        return false;
    }
    return run_less_on_path(entry->code_path);
}

static int run_tutorial_mode(const char *tutorial_root) {
    tutorial_index_t index = {0};
    if (!build_tutorial_index(tutorial_root, &index)) {
        fprintf(stderr, "Failed to build tutorial index from %s\n", tutorial_root);
        return 1;
    }

    enable_raw_mode();
    size_t toc_index = 0;
    bool showing_readme = false;
    bool tab_return_ready = false;
    render_toc(&index, toc_index);

    int ch;
    while ((ch = getchar()) != EOF) {
        if (!showing_readme) {
            if (ch == 'i' || ch == 'I') {
                if (toc_index > 0) {
                    toc_index--;
                    render_toc(&index, toc_index);
                }
            } else if (ch == 'k' || ch == 'K') {
                if (toc_index + 1 < index.count) {
                    toc_index++;
                    render_toc(&index, toc_index);
                }
            } else if (ch == '\r' || ch == '\n') {
                if (ensure_readme_loaded(&index.items[toc_index])) {
                    show_entry_readme_in_less(&index.items[toc_index]);
                    showing_readme = true;
                    tab_return_ready = false;
                    render_readme(&index, toc_index);
                }
            } else if (ch == 27) {
                break;
            }
        } else {
            tutorial_entry_t *entry = &index.items[toc_index];
            if (ch == 'i' || ch == 'I') {
                if (toc_index > 0) {
                    toc_index--;
                    if (ensure_readme_loaded(&index.items[toc_index])) {
                        show_entry_readme_in_less(&index.items[toc_index]);
                        tab_return_ready = false;
                        render_readme(&index, toc_index);
                    }
                }
            } else if (ch == 'k' || ch == 'K') {
                if (toc_index + 1 < index.count) {
                    toc_index++;
                    if (ensure_readme_loaded(&index.items[toc_index])) {
                        show_entry_readme_in_less(&index.items[toc_index]);
                        tab_return_ready = false;
                        render_readme(&index, toc_index);
                    }
                }
            } else if (ch == '\t') {
                if (!tab_return_ready) {
                    if (show_entry_code_in_less(entry)) {
                        tab_return_ready = true;
                    } else {
                        tab_return_ready = false;
                    }
                    render_readme(&index, toc_index);
                } else {
                    showing_readme = false;
                    tab_return_ready = false;
                    render_toc(&index, toc_index);
                }
            } else if (ch == 8 || ch == 127) {
                showing_readme = false;
                tab_return_ready = false;
                render_toc(&index, toc_index);
            } else if (ch == 27) {
                break;
            } else if (ch == '\r' || ch == '\n') {
                show_entry_readme_in_less(entry);
                tab_return_ready = false;
                render_readme(&index, toc_index);
            } else {
                tab_return_ready = false;
            }
        }
    }

    disable_raw_mode();
    ttak_mem_free_tutorial_index(&index);
    printf("\nGoodbye!\n");
    return 0;
}

static int run_manual_mode(const char *path) {
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
        }
    }

    disable_raw_mode();
    ttak_mem_free_help_doc(&doc);
    printf("\nGoodbye!\n");
    return 0;
}

static bool resolve_default_root(char *buffer, size_t size, const char *argv0) {
    if (!buffer || size == 0 || !argv0) return false;
    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) return false;
    char *slash = strrchr(resolved, '/');
    if (!slash) return false;
    *slash = '\0';
    slash = strrchr(resolved, '/');
    if (!slash) return false;
    *slash = '\0';
    if (strlen(resolved) + 1 > size) return false;
    strcpy(buffer, resolved);
    return true;
}

int main(int argc, char **argv) {
    const char *manual_path = NULL;
    const char *tutorial_root_override = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--manual") == 0) {
            if (i + 1 < argc) {
                manual_path = argv[++i];
            } else {
                fprintf(stderr, "--manual requires a path\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--tutorial-root") == 0) {
            if (i + 1 < argc) {
                tutorial_root_override = argv[++i];
            } else {
                fprintf(stderr, "--tutorial-root requires a path\n");
                return 1;
            }
        }
    }

    if (manual_path) {
        return run_manual_mode(manual_path);
    }

    char default_root[PATH_MAX];
    const char *root = tutorial_root_override;
    if (!root) {
        if (!resolve_default_root(default_root, sizeof(default_root), argv[0])) {
            root = "..";
        } else {
            root = default_root;
        }
    }
    return run_tutorial_mode(root);
}

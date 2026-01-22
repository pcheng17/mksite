#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "styles.h"

// clang-format off
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;
typedef int64_t  i64;
typedef int32_t  i32;
typedef int16_t  i16;
typedef int8_t   i8;
// clang-format on

#define LOG_ERROR(...) fprintf(stderr, "[ERROR] " __VA_ARGS__)
#define LOG_WARN(...) fprintf(stderr, "[WARN] " __VA_ARGS__)
#define LOG_INFO(...) fprintf(stderr, "[INFO] " __VA_ARGS__)

#define ALIGNMENT 16
#define ALIGN_UP_POW2(n, pow2) (((u64)(n) + ((u64)(pow2) - 1)) & (~((u64)(pow2) - 1)))

#define KB(n) ((u64)(n) << 10)
#define MB(n) ((u64)(n) << 20)

u64 get_page_size(void) {
#if defined(__APPLE__) || defined(__linux__)
    return (u64)getpagesize();
#endif
}

void* reserve_memory(u64 size) {
#if defined(__APPLE__) || defined(__linux__)
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

void* commit_memory(void* addr, u64 size) {
#if defined(__APPLE__) || defined(__linux__)
    // On MacOS and Linux, memory is committed on access, so just return the address
    return addr;
#endif
}

bool decommit_memory(void* addr, u64 size) {
#if defined(__APPLE__)
    // On MacOS, use madvise to indicate that the memory is no longer needed
    return madvise(addr, size, MADV_FREE) == 0;
#elif defined(__linux__)
    // On Linux, use MADV_DONTNEED instead of MADV_FREE
    return madvise(addr, size, MADV_DONTNEED) == 0;
#endif
}

bool release_memory(void* addr, u64 size) {
#if defined(__APPLE__) || defined(__linux__)
    return munmap(addr, size) == 0;
#endif
}

typedef struct {
    void* base;
    u64 reserved;
    u64 committed;
    u64 used;
} Arena;

Arena arena_create(u64 reserve_size) {
    const u64 page_size = get_page_size();
    reserve_size = ALIGN_UP_POW2(reserve_size, page_size);

    void* base = reserve_memory(reserve_size);
    if (base == NULL) {
        exit(1);
    }

    // clang-format off
    return (Arena){
        .base = base,
        .reserved = reserve_size,
        .committed = 0,
        .used = 0
    };
    // clang-format on
}

void* arena_push(Arena* arena, u64 size, u64 alignment) {
    assert((alignment & (alignment - 1)) == 0); // alignment must be power of 2

    const u64 aligned_used = ALIGN_UP_POW2(arena->used, alignment);
    const u64 newUsed = aligned_used + size;

    if (newUsed > arena->reserved) {
        LOG_ERROR(
            "Arena out of memory: requested %llu bytes, but only %llu bytes "
            "reserved\n",
            newUsed,
            arena->reserved);
        exit(1);
    }

    if (newUsed > arena->committed) {
        const u64 page_size = get_page_size();
        // No need to clamp `new_commit` here because `newUsed <= arena->reserved`,
        // and `arena->reserved` is is already aligned to page size. Therefore,
        // `new_commit` will also be <= `arena->reserved`.
        const u64 new_commit = ALIGN_UP_POW2(newUsed, page_size);
        char* start = (char*)arena->base + arena->committed;
        u64 commit_size = new_commit - arena->committed;
        if (commit_memory(start, commit_size) == NULL) {
            exit(1);
        }
        arena->committed = new_commit;
    }

    void* result = (char*)arena->base + aligned_used;
    arena->used = newUsed;
    return result;
}

void arena_clear(Arena* arena) {
    arena->used = 0;
}

bool arena_release(Arena* arena) {
    const bool result = release_memory(arena->base, arena->reserved);
    *arena = (Arena){0};
    return result;
}

#define PATH_MAX 1024
#define TITLE_MAX 256
#define DATE_MAX 64
#define PUBLIC_DIR "./public"
#define CONTENT_DIR "./content"
#define ASSET_DIR "./assets"

#define SITE_URL "journal.willcodeforboba.dev"

// clang-format off
const char* MONTHS_FULL[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

const char* MONTHS_ABBR[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};
// clang-format on

bool format_date(const char* iso_date, const char* dict[], char out[32]) {
    i32 year, month, day;
    if (sscanf(iso_date, "%d-%d-%d", &year, &month, &day) != 3) {
        return false;
    }
    assert(month >= 1 && month <= 12);
    snprintf(out, 32, "%s %2d, %04d", dict[month - 1], day, year);
    return true;
}

bool format_date_full(const char* iso_date, char out[32]) {
    return format_date(iso_date, MONTHS_FULL, out);
}

bool format_date_abbr(const char* iso_date, char out[32]) {
    return format_date(iso_date, MONTHS_ABBR, out);
}

typedef struct {
    char title[TITLE_MAX];
    char slug[TITLE_MAX];
    char date[DATE_MAX];
    const char* content;
} Page;

int compare_pages_desc(const void* a, const void* b) {
    const Page* page_a = (const Page*)a;
    const Page* page_b = (const Page*)b;
    return strcmp(page_b->date, page_a->date);
}

#define PRINT(...) fprintf(fout, __VA_ARGS__)

char* trim_leading_spaces(char* str) {
    while (*str == ' ') {
        str++;
    }
    return str;
}

void slugify(const char* input, char* output, u32 output_size) {
    u32 j = 0;
    bool prev_was_dash = true; // Start true to skip leading dashes

    for (u32 i = 0; input[i] && j < output_size - 1; ++i) {
        char c = input[i];

        if (isalnum(c)) {
            output[j++] = tolower(c);
            prev_was_dash = 0;
        } else if (!prev_was_dash) {
            output[j++] = '-';
            prev_was_dash = true;
        }
    }

    // Remove trailing dash
    if (j > 0 && output[j - 1] == '-') {
        j--;
    }

    output[j] = '\0';
}

bool is_blank_line(const char* line, u32 len) {
    for (u32 i = 0; i < len; ++i) {
        if (line[i] != ' ' && line[i] != '\t') {
            return false;
        }
    }
    return true;
}

bool is_code_fence(const char* line, u32 len) {
    return len >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`';
}

u8 get_heading_level(const char* line, u32 len) {
    u8 level = 0;
    while (level < len && line[level] == '#') {
        ++level;
    }
    if (level > 0 && level <= 6 && level < len && line[level] == ' ') {
        return level;
    }
    return 0;
}

bool is_unordered_list_item(const char* line, u32 len) {
    return len >= 2 && line[0] == '-' && line[1] == ' ';
}

bool is_ordered_list_item(const char* line, u32 len) {
    u32 i = 0;
    while (i < len && isdigit(line[i])) {
        ++i;
    }
    return i > 0 && i + 1 < len && line[i] == '.' && line[i + 1] == ' ';
}

u32 get_line_len(const char* cursor) {
    const char* eol = strchr(cursor, '\n');
    return eol ? (u32)(eol - cursor) : (u32)strlen(cursor);
}

// Collect paragraph text: consecutive non-blank, non-special lines joined with spaces
// Returns length of collected text, advances *cursor past consumed input
u32 collect_paragraph(const char** cursor, char* out, u32 out_size) {
    u32 out_len = 0;
    bool first_line = true;

    while (**cursor) {
        u32 line_len = get_line_len(*cursor);

        // Stop conditions: blank line or special line types
        if (line_len == 0 || is_blank_line(*cursor, line_len)) {
            break;
        }
        if (is_code_fence(*cursor, line_len)) {
            break;
        }
        if (get_heading_level(*cursor, line_len) > 0) {
            break;
        }
        if (is_unordered_list_item(*cursor, line_len)) {
            break;
        }
        if (is_ordered_list_item(*cursor, line_len)) {
            break;
        }

        // Add space between lines (not before first line)
        if (!first_line && out_len < out_size - 1) {
            out[out_len++] = ' ';
        }
        first_line = false;

        // Copy line content
        u32 copy_len = (out_len + line_len < out_size - 1) ? line_len : (out_size - 1 - out_len);
        memcpy(out + out_len, *cursor, copy_len); // TODO REMOVE MEMCPY
        out_len += copy_len;

        // Advance cursor past line and newline
        *cursor += line_len;
        if (**cursor == '\n') {
            (*cursor)++;
        }
    }

    out[out_len] = '\0';
    return out_len;
}

// Collect code block content until closing fence
// Returns length of collected text, advances *cursor past closing fence
u32 collect_code_block(const char** cursor, char* out, u32 out_size) {
    u32 out_len = 0;

    while (**cursor) {
        u32 line_len = get_line_len(*cursor);

        // Check for closing fence
        if (is_code_fence(*cursor, line_len)) {
            *cursor += line_len;
            if (**cursor == '\n') {
                (*cursor)++;
            }
            break;
        }

        // Copy line content (preserve newlines in code blocks)
        u32 copy_len = (out_len + line_len < out_size - 1) ? line_len : (out_size - 1 - out_len);
        memcpy(out + out_len, *cursor, copy_len);
        out_len += copy_len;

        // Advance cursor
        *cursor += line_len;
        if (**cursor == '\n') {
            (*cursor)++;
            if (out_len < out_size - 1) {
                out[out_len++] = '\n';
            }
        }
    }

    // Remove trailing newline if present
    if (out_len > 0 && out[out_len - 1] == '\n') {
        out_len--;
    }
    out[out_len] = '\0';
    return out_len;
}

// Get the text offset after list marker (e.g., "- " returns 2, "1. " returns 3)
u32 get_list_item_offset(const char* line, u32 len) {
    if (is_unordered_list_item(line, len)) {
        return 2; // "- "
    }
    if (is_ordered_list_item(line, len)) {
        u32 i = 0;
        while (i < len && isdigit(line[i])) {
            ++i;
        }
        return i + 2; // digits + ". "
    }
    return 0;
}

char* read_file(Arena* arena, const char* path, u64* file_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = arena_push(arena, *file_len + 1, ALIGNMENT);
    fread(content, 1, *file_len, f);
    content[*file_len] = '\0'; // null-terminate
    fclose(f);
    return content;
}

u32 import_pages(const char* dir_path, Arena* arena, Page** out_pages) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        LOG_ERROR("Failed to open directory: %s\n", dir_path);
        return 0;
    }

    // First pass: count pages
    u32 page_count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        const u32 len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".txt") == 0) {
            ++page_count;
        }
    }

    LOG_INFO("Scanned %s: found %u pages\n", dir_path, page_count);

    // Allocate array of pages
    *out_pages = (Page*)arena_push(arena, sizeof(Page) * page_count, ALIGNMENT);

    // Second pass: populate pages
    rewinddir(dir);
    u32 idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        const u32 len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".txt") == 0) {
            LOG_INFO("Importing page: %s\n", name);

            Page* page = &(*out_pages)[idx++];

            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s/%s", dir_path, name);

            u64 file_len = 0;
            const char* data = read_file(arena, full_path, &file_len);
            if (!data) {
                LOG_ERROR("Failed to read %s\n", full_path);
                closedir(dir);
                return 0;
            }

            char* start = (char*)data;
            char* end = start + file_len;
            while (start < end) {
                char* line = memchr(start, '\n', end - start);
                u64 line_len = line ? (line - start) : (end - start);

                // End of metadata
                if (line_len == 3 && strncmp(start, "---", 3) == 0) {
                    start = line ? line + 1 : end;
                    break;
                }

                if (strncmp(start, "title:", 6) == 0) {
                    char* value = trim_leading_spaces(start + 6);
                    snprintf(
                        page->title,
                        sizeof(page->title),
                        "%.*s",
                        (int)(line_len - (value - start)),
                        value);
                    slugify(page->title, page->slug, sizeof(page->slug));
                } else if (strncmp(start, "date:", 5) == 0) {
                    char* value = trim_leading_spaces(start + 5);
                    snprintf(
                        page->date,
                        sizeof(page->date),
                        "%.*s",
                        (int)(line_len - (value - start)),
                        value);
                }
                start = line ? line + 1 : end;
            }

            page->content = start;
        }
    }

    return page_count;
}

typedef enum {
    FORMAT_NONE,
    FORMAT_BOLD,
    FORMAT_ITALIC,
    FORMAT_HIGHLIGHT,
    FORMAT_INLINE_CODE,
    FORMAT_SIDENOTE,
    FORMAT_MARGIN_NOTE,
    FORMAT_TYPE_COUNT
} FormatType;

typedef enum {
    BLOCK_NONE,
    BLOCK_CODE,
    BLOCK_UNORDERED_LIST,
    BLOCK_ORDERED_LIST,
} BlockType;

typedef struct {
    bool in_section;
    bool in_paragraph;
    BlockType block;
    u32 sidenote_id;
} ParseState;

FormatType get_format_type(const char* text, u32 pos, u32 len) {
    if (pos + 1 < len && text[pos] == '*' && text[pos + 1] == '*') {
        return FORMAT_BOLD;
    } else if (pos + 1 < len && text[pos] == '_' && text[pos + 1] == '_') {
        return FORMAT_ITALIC;
    } else if (pos + 1 < len && text[pos] == '=' && text[pos + 1] == '=') {
        return FORMAT_HIGHLIGHT;
    } else if (text[pos] == '`' && pos + 1 < len && text[pos + 1] != '`') {
        return FORMAT_INLINE_CODE;
    } else if (pos + 2 < len && text[pos] == '^' && text[pos + 1] == '-' && text[pos + 2] == '[') {
        return FORMAT_MARGIN_NOTE;
    } else if (pos + 1 < len && text[pos] == '^' && text[pos + 1] == '[') {
        return FORMAT_SIDENOTE;
    }
    return FORMAT_NONE;
}

void write_html_escaped(FILE* fout, const char* text, u32 len) {
    for (u32 i = 0; i < len; ++i) {
        switch (text[i]) {
            case '<': fprintf(fout, "&lt;"); break;
            case '>': fprintf(fout, "&gt;"); break;
            case '&': fprintf(fout, "&amp;"); break;
            default: fputc(text[i], fout); break;
        }
    }
}

// Find matching closing bracket, accounting for nested brackets
// Returns position of ']' or len if not found
u32 find_closing_bracket(const char* text, u32 start, u32 len) {
    u32 depth = 1;
    for (u32 i = start; i < len; ++i) {
        if (text[i] == '[') {
            depth++;
        } else if (text[i] == ']') {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return len;
}

void write_formatted_content(FILE* fout, const char* text, u32 len, ParseState* state);

void write_sidenote(FILE* fout, const char* content, u32 content_len, ParseState* state) {
    u32 id = ++state->sidenote_id;
    fprintf(fout, "<label for=\"sn-%u\" class=\"margin-toggle sidenote-number\"></label>", id);
    fprintf(fout, "<input type=\"checkbox\" id=\"sn-%u\" class=\"margin-toggle\"/>", id);
    fprintf(fout, "<span class=\"sidenote\">");
    write_formatted_content(fout, content, content_len, state);
    fprintf(fout, "</span>");
}

void write_margin_note(FILE* fout, const char* content, u32 content_len, ParseState* state) {
    u32 id = ++state->sidenote_id;
    fprintf(fout, "<label for=\"mn-%u\" class=\"margin-toggle\">&#8853;</label>", id);
    fprintf(fout, "<input type=\"checkbox\" id=\"mn-%u\" class=\"margin-toggle\"/>", id);
    fprintf(fout, "<span class=\"marginnote\">");
    write_formatted_content(fout, content, content_len, state);
    fprintf(fout, "</span>");
}

void write_formatted_content(FILE* fout, const char* text, u32 len, ParseState* state) {
    bool in_bold = false;
    bool in_italic = false;
    bool in_highlight = false;

    for (u32 i = 0; i < len; ++i) {
        FormatType fmt_type = get_format_type(text, i, len);
        switch (fmt_type) {
            case FORMAT_BOLD:
                if (in_bold) {
                    fprintf(fout, "</strong>");
                    in_bold = false;
                } else {
                    fprintf(fout, "<strong>");
                    in_bold = true;
                }
                ++i; // skip next '*'
                continue;
            case FORMAT_ITALIC:
                if (in_italic) {
                    fprintf(fout, "</em>");
                    in_italic = false;
                } else {
                    fprintf(fout, "<em>");
                    in_italic = true;
                }
                i += 1; // skip next '_'
                continue;
            case FORMAT_HIGHLIGHT:
                if (in_highlight) {
                    fprintf(fout, "</mark>");
                    in_highlight = false;
                } else {
                    fprintf(fout, "<mark>");
                    in_highlight = true;
                }
                i += 1; // skip next '='
                continue;
            case FORMAT_INLINE_CODE: {
                u32 j = i + 1;
                while (j < len && text[j] != '`') {
                    ++j;
                }
                if (j < len) {
                    u32 code_len = j - (i + 1);
                    fprintf(fout, "<code>%.*s</code>", code_len, text + i + 1);
                    i = j;
                } else {
                    fputc(text[i], fout);
                }
                continue;
            }
            case FORMAT_SIDENOTE: {
                // ^[content]
                u32 content_start = i + 2; // skip "^["
                u32 close = find_closing_bracket(text, content_start, len);
                if (close < len) {
                    write_sidenote(fout, text + content_start, close - content_start, state);
                    i = close;
                } else {
                    fputc(text[i], fout);
                }
                continue;
            }
            case FORMAT_MARGIN_NOTE: {
                // ^-[content]
                u32 content_start = i + 3; // skip "^-["
                u32 close = find_closing_bracket(text, content_start, len);
                if (close < len) {
                    write_margin_note(fout, text + content_start, close - content_start, state);
                    i = close;
                } else {
                    fputc(text[i], fout);
                }
                continue;
            }
            case FORMAT_NONE:
            default:
                fputc(text[i], fout);
                break;
        }
    }

    if (in_bold) {
        fprintf(fout, "</strong>");
    }
    if (in_italic) {
        fprintf(fout, "</em>");
    }
    if (in_highlight) {
        fprintf(fout, "</mark>");
    }
}

void html_write_head(FILE* fout, const char* title) {
    PRINT("<!DOCTYPE html>\n");
    PRINT("<html lang=\"en\">\n");
    PRINT("<head>\n");
    PRINT("  <meta charset=\"utf-8\">\n");
    PRINT("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
    PRINT("  <link rel=\"icon\" type=\"image/svg+xml\" href=\"/favicon.svg\" />\n");
    PRINT("  <title>%s</title>\n", title);
    PRINT("  <style>\n%.*s\n</style>\n", styles_css_len, styles_css);
    PRINT("</head>\n");
}

void html_write_header(FILE* fout) {
    PRINT("<header>\n");
    PRINT("  <nav>\n");
    PRINT("    <a href=\"/\">%s</a>\n", SITE_URL);
    PRINT("  </nav>\n");
    PRINT("</header>\n");
}

typedef struct {
    u32 text_offset;
    u8 level;
} HeadingInfo;

HeadingInfo get_heading_info(const char* line, u32 len) {
    HeadingInfo info = {0, 0};
    u32 i = 0;

    while (i < len && line[i] == '#') {
        ++info.level;
        ++i;
    }

    if (i < len && line[i] == ' ') i++;

    if (info.level > 0 && info.level <= 6) {
        info.text_offset = i;
    } else {
        info.level = 0;
    }

    return info;
}

#define BUFFER_SIZE 8192

void close_paragraph(FILE* fout, ParseState* state) {
    if (state->in_paragraph) {
        fprintf(fout, "</p>\n");
        state->in_paragraph = false;
    }
}

void close_list(FILE* fout, ParseState* state) {
    if (state->block == BLOCK_UNORDERED_LIST) {
        fprintf(fout, "</ul>\n");
        state->block = BLOCK_NONE;
    } else if (state->block == BLOCK_ORDERED_LIST) {
        fprintf(fout, "</ol>\n");
        state->block = BLOCK_NONE;
    }
}

void close_section(FILE* fout, ParseState* state) {
    close_paragraph(fout, state);
    close_list(fout, state);
    if (state->in_section) {
        fprintf(fout, "</section>\n");
        state->in_section = false;
    }
}

void build_page(FILE* fout, const Page* page) {
    char formatted_date[32];
    if (page->date[0] && !format_date_full(page->date, formatted_date)) {
        LOG_WARN("Invalid date format in page %s: %s\n", page->slug, page->date);
        formatted_date[0] = '\0';
    }

    // Output HTML
    // clang-format off
    html_write_head(fout, page->title);
    fprintf(fout, "<body>\n");
    fprintf(fout, "<article>\n");
    fprintf(fout, "<h1>%s</h1>\n", page->title);
    if (page->date[0]) {
        fprintf(fout, "<p class=\"subtitle\">%s</p>\n", formatted_date);
    }
    // clang-format on

    ParseState state = {0};
    char buffer[BUFFER_SIZE];
    const char* cursor = page->content;

    while (*cursor) {
        u32 line_len = get_line_len(cursor);

        // Blank line
        if (line_len == 0 || is_blank_line(cursor, line_len)) {
            close_paragraph(fout, &state);
            close_list(fout, &state);
            cursor += line_len;
            if (*cursor == '\n') cursor++;
            continue;
        }

        // Code fence
        if (is_code_fence(cursor, line_len)) {
            close_paragraph(fout, &state);
            close_list(fout, &state);
            // Skip opening fence
            cursor += line_len;
            if (*cursor == '\n') cursor++;
            // Collect code block
            u32 code_len = collect_code_block(&cursor, buffer, BUFFER_SIZE);
            fprintf(fout, "<pre><code>");
            write_html_escaped(fout, buffer, code_len);
            fprintf(fout, "</code></pre>\n");
            continue;
        }

        // Heading
        u8 heading_level = get_heading_level(cursor, line_len);
        if (heading_level > 0) {
            close_paragraph(fout, &state);
            close_list(fout, &state);

            // h2 starts a new section
            if (heading_level == 2) {
                close_section(fout, &state);
                fprintf(fout, "<section>\n");
                state.in_section = true;
            }

            // Extract heading text (skip "## ")
            u32 text_offset = heading_level + 1;
            u32 text_len = line_len - text_offset;
            fprintf(fout, "<h%u>", heading_level);
            write_formatted_content(fout, cursor + text_offset, text_len, &state);
            fprintf(fout, "</h%u>\n", heading_level);

            cursor += line_len;
            if (*cursor == '\n') cursor++;
            continue;
        }

        // Unordered list item
        if (is_unordered_list_item(cursor, line_len)) {
            close_paragraph(fout, &state);

            if (state.block != BLOCK_UNORDERED_LIST) {
                close_list(fout, &state);
                fprintf(fout, "<ul>\n");
                state.block = BLOCK_UNORDERED_LIST;
            }

            u32 item_offset = get_list_item_offset(cursor, line_len);
            fprintf(fout, "<li>");
            write_formatted_content(fout, cursor + item_offset, line_len - item_offset, &state);
            fprintf(fout, "</li>\n");

            cursor += line_len;
            if (*cursor == '\n') cursor++;
            continue;
        }

        // Ordered list item
        if (is_ordered_list_item(cursor, line_len)) {
            close_paragraph(fout, &state);

            if (state.block != BLOCK_ORDERED_LIST) {
                close_list(fout, &state);
                fprintf(fout, "<ol>\n");
                state.block = BLOCK_ORDERED_LIST;
            }

            u32 item_offset = get_list_item_offset(cursor, line_len);
            fprintf(fout, "<li>");
            write_formatted_content(fout, cursor + item_offset, line_len - item_offset, &state);
            fprintf(fout, "</li>\n");

            cursor += line_len;
            if (*cursor == '\n') cursor++;
            continue;
        }

        // Paragraph text - collect full paragraph
        close_list(fout, &state);
        u32 para_len = collect_paragraph(&cursor, buffer, BUFFER_SIZE);
        if (para_len > 0) {
            fprintf(fout, "<p>");
            write_formatted_content(fout, buffer, para_len, &state);
            fprintf(fout, "</p>\n");
        }
    }

    // Close any remaining open elements
    close_section(fout, &state);

    fprintf(fout, "</article>\n");
    fprintf(fout, "</body>\n");
    fprintf(fout, "</html>\n");
}

bool build_pages(const char* dst_path, Page* pages, u32 page_count) {
    for (u32 i = 0; i < page_count; ++i) {
        Page* page = &pages[i];

        char out_path[PATH_MAX];
        int len = snprintf(out_path, sizeof(out_path), "%s/%s.html", dst_path, page->slug);
        assert(len > 0 && len < (int)sizeof(out_path));

        FILE* fout = fopen(out_path, "w");
        if (!fout) {
            LOG_ERROR("Failed to open %s for writing\n", out_path);
            return false;
        }

        build_page(fout, page);

        fclose(fout);
    }

    return true;
}

/// @brief Create the public directory if it doesn't exist
bool prepare_public_dir() {
    if (access(PUBLIC_DIR, F_OK) == -1) {
        if (mkdir(PUBLIC_DIR, 0755) == -1) {
            fprintf(stderr, "Failed to create %s\n", PUBLIC_DIR);
            return false;
        }
    }
    return true;
}

bool build_index(Page* pages, u32 page_count) {
    char index_path[PATH_MAX];
    snprintf(index_path, PATH_MAX, "%s/index.html", PUBLIC_DIR);

    FILE* fout = fopen(index_path, "w");
    if (!fout) {
        LOG_ERROR("Failed to open %s for writing\n", index_path);
        return false;
    }

    // Output HTML header
    // clang-format off
    html_write_head(fout, "Blog Index");
    PRINT("<body>\n");
    // html_write_header(fout);
    PRINT("  <h1>Blog Posts</h1>\n");
    PRINT("  <table class=\"archive\">\n");
    PRINT("    <thead><tr><th>date</th><th>title</th><th>tags</th></tr></thead>\n");
    PRINT("      <tbody>\n");
    for (u32 i = 0; i < page_count; ++i) {
        const Page* page = &pages[i];
        char formatted_date[32];
        if (page->date[0] && !format_date_abbr(page->date, formatted_date)) {
            LOG_WARN("Invalid date format in page %s: %s\n", page->slug, page->date);
            formatted_date[0] = '\0';
        }
        PRINT("        <tr>\n");
        PRINT("          <td class=\"date\">%s</td>\n", formatted_date);
        PRINT("          <td class=\"title\"><a href=\"posts/%s.html\">%s</a></td>\n", page->slug, page->title);
        PRINT("        </tr>\n");
    }
    PRINT("    </tbody>\n");
    PRINT("  </table>\n");
    PRINT("</body>\n");
    PRINT("</html>\n");
    // clang-format on

    fclose(fout);
    return true;
}

bool install_favicon() {
    const char* src_favicon_path = ASSET_DIR "/favicon.svg";
    const char* dst_favicon_path = PUBLIC_DIR "/favicon.svg";

    FILE* src_favicon = fopen(src_favicon_path, "rb");
    if (!src_favicon) {
        LOG_ERROR("Failed to open favicon source: %s\n", src_favicon_path);
        return false;
    }

    FILE* dst_favicon = fopen(dst_favicon_path, "wb");
    if (!dst_favicon) {
        LOG_ERROR("Failed to open favicon destination: %s\n", dst_favicon_path);
        fclose(src_favicon);
        return false;
    }

    char buffer[4096];
    u64 bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_favicon)) > 0) {
        fwrite(buffer, 1, bytes, dst_favicon);
    }

    fclose(src_favicon);
    fclose(dst_favicon);
    return true;
}

int main(int argc, char** argv) {
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    if (!prepare_public_dir()) {
        return 1;
    }

    if (!install_favicon()) {
        return 1;
    }

    DIR* dir = opendir(CONTENT_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open content directory: %s\n", CONTENT_DIR);
        return 1;
    }

    Arena arena = arena_create(KB(16));

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* dname = entry->d_name;

        if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0) {
            continue;
        }

        if (entry->d_type == DT_DIR) {
            char src_path[PATH_MAX];
            char dst_path[PATH_MAX];
            snprintf(src_path, PATH_MAX, "%s/%s", CONTENT_DIR, dname);
            snprintf(dst_path, PATH_MAX, "%s/%s", PUBLIC_DIR, dname);

            Page* pages = NULL;
            u32 page_count = import_pages(src_path, &arena, &pages);

            if (page_count == 0) {
                LOG_ERROR("Failed to import pages from %s\n", src_path);
                arena_release(&arena);
                closedir(dir);
                return 1;
            }

            if (access(dst_path, F_OK) == -1) {
                if (mkdir(dst_path, 0755) == -1) {
                    LOG_ERROR("Failed to create directory: %s\n", dst_path);
                    arena_release(&arena);
                    closedir(dir);
                    return 1;
                }
            }

            if (!build_pages(dst_path, pages, page_count)) {
                LOG_ERROR("Failed to build pages to %s\n", dst_path);
                arena_release(&arena);
                closedir(dir);
                return 1;
            }

            // If we're processing the `posts` directory, also build an index.html
            if (strcmp(dname, "posts") == 0) {
                qsort(pages, page_count, sizeof(Page), compare_pages_desc);
                build_index(pages, page_count);
            }
        }

        // We can clear the arena after each directory is processed
        arena_clear(&arena);
    }

    arena_release(&arena);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000.0 + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    printf("Site built in %.3f ms\n", elapsed_ms);

    return 0;
}

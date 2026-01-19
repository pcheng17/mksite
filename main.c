#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

#define ALIGNMENT 16
#define ALIGN_UP_POW2(n, pow2) (((u64)(n) + ((u64)(pow2) - 1)) & (~((u64)(pow2) - 1)))

#define KB(n) ((u64)(n) << 10)
#define MB(n) ((u64)(n) << 20)

u64 get_page_size(void) {
#if defined(__APPLE__)
    return (u64)getpagesize();
#endif
}

void* reserve_memory(u64 size) {
#if defined(__APPLE__)
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

void* commit_memory(void* addr, u64 size) {
#if defined(__APPLE__)
    // On macOS, memory is committed on access, so just return the address
    return addr;
#endif
}

bool decommit_memory(void* addr, u64 size) {
#if defined(__APPLE__)
    // On macOS, we can use madvise to indicate that the memory is no longer needed
    return madvise(addr, size, MADV_FREE) == 0;
#endif
}

bool release_memory(void* addr, u64 size) {
#if defined(__APPLE__)
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

#define PRINT(...) fprintf(fout, __VA_ARGS__)

#define BUILD_DIR "./public"

int main(int argc, char** argv) {
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    // Check if build dir exists, if not create it
    if (access(BUILD_DIR, F_OK) == -1) {
        if (mkdir(BUILD_DIR, 0755) == -1) {
            fprintf(stderr, "Failed to create %s\n", BUILD_DIR);
            return 1;
        }
    }

    Arena arena = arena_create(KB(1));

    u64 file_len = 0;
    const char* content = read_file(&arena, "blog.txt", &file_len);
    if (!content) {
        fprintf(stderr, "Failed to read blog.txt\n");
        arena_release(&arena);
        return 1;
    }

    char* cursor = (char*)content;
    char* line;

    char title[128];
    char date[64];
    while ((line = strsep(&cursor, "\n")) != NULL) {
        line[strcspn(line, "\n")] = '\0'; // Replace newline with null terminator
        if (strcmp(line, "---") == 0) break;
        if (strncmp(line, "title:", 6) == 0) {
            snprintf(title, sizeof(title), "%s", line + 6);
        } else if (strncmp(line, "date:", 5) == 0) {
            snprintf(date, sizeof(date), "%s", line + 5);
        }
    }

    char out_path[256];
    int len = snprintf(out_path, sizeof(out_path), "%s/index.html", BUILD_DIR);
    assert(len > 0 && len < (int)sizeof(out_path));

    FILE* fout = fopen(out_path, "w");
    if (!fout) {
        fprintf(stderr, "Failed to open %s for writing\n", out_path);
        arena_release(&arena);
        return 1;
    }

    // Output HTML
    // clang-format off
    PRINT("<!DOCTYPE html>\n");
    PRINT("<html lang=\"en\">\n");
    PRINT("<head>\n");
    PRINT("  <meta charset=\"utf-8\">\n");
    PRINT("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
    PRINT("  <title>%s</title>\n", title);
    PRINT("  <style>\n");
    PRINT("    body { max-width: 760px; margin: 2em auto; padding: 0 1em; font-size: 14px; font-family: \"Lucida Grande\", sans-serif; color: rgb(51, 51, 51); }\n");
    PRINT("    p { line-height: 1.5; }\n");
    PRINT("  </style>\n");
    PRINT("</head>\n");
    PRINT("<body>\n");
    PRINT("  <article>\n");
    PRINT("    <h1>%s</h1>\n", title);
    if (date[0]) {
        PRINT("    <time>%s</time>\n", date);
    }
    // clang-format on

    bool in_paragraph = false;

    while (*cursor) {
        // Find end of line
        char* eol = strchr(cursor, '\n');
        int len = eol ? (int)(eol - cursor) : strlen(cursor);

        if (len == 0) {
            if (in_paragraph) {
                PRINT("</p>\n");
                in_paragraph = false;
            }
        } else {
            if (!in_paragraph) {
                PRINT("    <p>");
                in_paragraph = true;
            } else {
                PRINT(" ");
            }
            PRINT("%.*s", len, cursor); // print exactly len chars
        }

        cursor += eol ? len + 1 : len; // skip past newline if present
    }

    if (in_paragraph) {
        PRINT("</p>\n");
    }
    PRINT("  </article>\n");
    PRINT("</body>\n");
    PRINT("</html>\n");

    fclose(fout);
    arena_release(&arena);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000.0 + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    printf("Site built in %.3f ms\n", elapsed_ms);

    return 0;
}

#ifndef HTML_H
#define HTML_H

#include <stdio.h>
#include <stdarg.h>

typedef int32_t i32;

typedef struct {
    i32 indent;
    FILE* f;
} Html;

Html* html_raw(Html* h, const char* fmt, ...) {
    fprintf(h->f, "%*s", h->indent, "");
    va_list args;
    va_start(args, fmt);
    vfprintf(h->f, fmt, args);
    va_end(args);
    return h;
}

Html* html_open(Html* h, const char* tag, const char* attrs) {
    if (attrs) {
        html_raw(h, "<%s %s>\n", tag, attrs);
    } else {
        html_raw(h, "<%s>\n", tag);
    }
    h->indent += 2;
    return h;
}

Html* html_close(Html* h, const char* tag) {
    h->indent -= 2;
    return html_raw(h, "</%s>\n", tag);
}

/// @brief Write a self-closing HTML tag
Html* html_void(Html* h, const char* tag, const char* attrs) {
    if (attrs) {
        return html_raw(h, "<%s %s />\n", tag, attrs);
    } else {
        return html_raw(h, "<%s />\n", tag);
    }
}

// Inline content (no newline after open, content on same line)
Html* html_inline(Html* h, const char* tag, const char* attrs, const char* content) {
    if (attrs) {
        return html_raw(h, "<%s %s>%s</%s>\n", tag, attrs, content, tag);
    } else {
        return html_raw(h, "<%s>%s</%s>\n", tag, content, tag);
    }
}

#endif // HTML_H

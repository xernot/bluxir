/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "util.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

void safe_strcpy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void format_time(int seconds, char *buf, size_t buf_size)
{
    if (seconds <= 0) {
        safe_strcpy(buf, "0:00", buf_size);
        return;
    }
    int mins = seconds / 60;
    int secs = seconds % 60;
    snprintf(buf, buf_size, "%d:%02d", mins, secs);
}

void url_encode(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 3 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else {
            snprintf(dst + j, dst_size - j, "%%%02X", c);
            j += 3;
        }
    }
    dst[j] = '\0';
}

void url_encode_param(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 3 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '&') {
            snprintf(dst + j, dst_size - j, "%%%02X", c);
            j += 3;
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

int strcasecmp_portable(const char *a, const char *b)
{
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0) return d;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool str_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

char *str_strip(char *s)
{
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

void create_volume_bar(int volume, int width, char *buf, size_t buf_size)
{
    if (buf_size < (size_t)(width + 3)) return;
    int filled = volume * width / 100;
    buf[0] = '[';
    for (int i = 0; i < width; i++) {
        buf[1 + i] = (i < filled) ? '#' : '-';
    }
    buf[1 + width] = ']';
    buf[2 + width] = '\0';
}

bool parse_quality(const char *stream_format, int *bit_depth, double *sample_rate)
{
    if (!stream_format) return false;
    /* Look for pattern like "24/96" or "16/44.1" */
    const char *p = stream_format;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            int bd = 0;
            const char *start = p;
            while (isdigit((unsigned char)*p)) {
                bd = bd * 10 + (*p - '0');
                p++;
            }
            if (*p == '/') {
                p++;
                double sr = 0;
                int has_dot = 0;
                double frac = 1.0;
                while (isdigit((unsigned char)*p) || (*p == '.' && !has_dot)) {
                    if (*p == '.') {
                        has_dot = 1;
                    } else if (has_dot) {
                        frac *= 0.1;
                        sr += (*p - '0') * frac;
                    } else {
                        sr = sr * 10 + (*p - '0');
                    }
                    p++;
                }
                if (bd > 0 && sr > 0) {
                    if (bit_depth) *bit_depth = bd;
                    if (sample_rate) *sample_rate = sr;
                    return true;
                }
            }
            /* Not a quality pattern, keep scanning */
            p = start + 1;
            continue;
        }
        p++;
    }
    return false;
}

void derive_quality(const char *stream_format, char *buf, size_t buf_size)
{
    if (!stream_format || !stream_format[0]) {
        safe_strcpy(buf, "-", buf_size);
        return;
    }
    if (str_contains_ci(stream_format, "MQA")) {
        safe_strcpy(buf, "MQA", buf_size);
        return;
    }
    if (str_contains_ci(stream_format, "MP3")) {
        safe_strcpy(buf, "MP3", buf_size);
        return;
    }
    int bd;
    double sr;
    if (parse_quality(stream_format, &bd, &sr)) {
        if (bd > 16 || sr > 44.1) {
            safe_strcpy(buf, "Hi-Res", buf_size);
        } else {
            safe_strcpy(buf, "CD-Quality", buf_size);
        }
        return;
    }
    safe_strcpy(buf, "-", buf_size);
}

int str_count_char(const char *str, char ch)
{
    int count = 0;
    while (*str) {
        if (*str == ch) count++;
        str++;
    }
    return count;
}

int int_min(int a, int b) { return a < b ? a : b; }
int int_max(int a, int b) { return a > b ? a : b; }

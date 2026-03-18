/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdbool.h>

/* Copy src into dst, ensuring null-termination within dst_size */
void safe_strcpy(char *dst, const char *src, size_t dst_size);

/* Format seconds as "M:SS" into buf (buf_size should be >= 16) */
void format_time(int seconds, char *buf, size_t buf_size);

/* URL-encode src into dst (dst_size should be >= 3*strlen(src)+1) */
void url_encode(const char *src, char *dst, size_t dst_size);

/* Encode only & and = in a query parameter value (preserves : / % etc.) */
void url_encode_param(const char *src, char *dst, size_t dst_size);

/* Case-insensitive string comparison */
int strcasecmp_portable(const char *a, const char *b);

/* Check if needle is found in haystack (case-insensitive) */
bool str_contains_ci(const char *haystack, const char *needle);

/* Get current time as a double (seconds since epoch) */
double now_sec(void);

/* Strip leading/trailing whitespace in-place, return pointer to start */
char *str_strip(char *s);

/* Create volume bar like "[####--------]" in buf */
void create_volume_bar(int volume, int width, char *buf, size_t buf_size);

/* Parse bit_depth and sample_rate from stream format like "FLAC 24/96" */
bool parse_quality(const char *stream_format, int *bit_depth, double *sample_rate);

/* Derive quality label from stream format (Hi-Res, CD-Quality, MQA, MP3, -) */
void derive_quality(const char *stream_format, char *buf, size_t buf_size);

/* Count occurrences of ch in str */
int str_count_char(const char *str, char ch);

/* Min/max helpers */
int int_min(int a, int b);
int int_max(int a, int b);

#endif /* UTIL_H */

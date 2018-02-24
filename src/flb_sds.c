/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * The following SDS interface is a clone/strip-down version of the original
 * SDS library created by Antirez at https://github.com/antirez/sds.
 */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_log.h>
#include <fluent-bit/flb_sds.h>
#include <fluent-bit/flb_utf8.h>

static flb_sds_t sds_alloc(size_t size)
{
    void *buf;
    flb_sds_t s;
    struct flb_sds *head;

    buf = flb_malloc(FLB_SDS_HEADER_SIZE + size + 1);
    if (!buf) {
        flb_errno();
        return NULL;
    }

    head = buf;
    head->len = 0;
    head->alloc = size;

    s = head->buf;
    *s = '\0';

    return s;
}

flb_sds_t flb_sds_create_len(char *str, int len)
{
    flb_sds_t s;
    struct flb_sds *head;

    s = sds_alloc(len);
    if (!s) {
        return NULL;
    }

    if (str) {
        memcpy(s, str, len);
        s[len] = '\0';

        head = FLB_SDS_HEADER(s);
        head->len = len;
    }
    return s;
}

flb_sds_t flb_sds_create(char *str)
{
    size_t len;

    if (!str) {
        len = 0;
    }
    else {
        len = strlen(str);
    }

    return flb_sds_create_len(str, len);
}

flb_sds_t flb_sds_create_size(size_t size)
{
    return sds_alloc(size);
}

/* Increase SDS buffer size 'len' bytes */
flb_sds_t flb_sds_increase(flb_sds_t s, size_t len)
{
    size_t new_size;
    struct flb_sds *head;
    flb_sds_t out;
    void *tmp;

    out = s;
    new_size = (FLB_SDS_HEADER_SIZE + flb_sds_alloc(s) + len + 1);
    head = FLB_SDS_HEADER(s);
    tmp = flb_realloc(head, new_size);
    if (!tmp) {
        flb_errno();
        return NULL;
    }

    if (tmp != head) {
        head = tmp;
    }

    head->alloc += len;
    out = head->buf;

    return out;
}

flb_sds_t flb_sds_cat(flb_sds_t s, char *str, int len)
{
    size_t avail;
    struct flb_sds *head;
    flb_sds_t tmp = NULL;

    avail = flb_sds_avail(s);
    if (avail < len) {
        tmp = flb_sds_increase(s, len);
        if (!tmp) {
            return NULL;
        }
        s = tmp;
    }
    memcpy((char *) (s + flb_sds_len(s)), str, len);

    head = FLB_SDS_HEADER(s);
    head->len += len;
    s[head->len] = '\0';

    return s;
}

flb_sds_t flb_sds_copy(flb_sds_t s, char *str, int len)
{
    size_t avail;
    struct flb_sds *head;
    flb_sds_t tmp = NULL;

    avail = flb_sds_alloc(s);
    if (avail < len) {
        tmp = flb_sds_increase(s, len);
        if (!tmp) {
            return NULL;
        }
        s = tmp;
    }
    memcpy((char *) s, str, len);

    head = FLB_SDS_HEADER(s);
    head->len = len;
    s[head->len] = '\0';

    return s;
}

flb_sds_t flb_sds_cat_utf8 (flb_sds_t s, char *str, int str_len)
{
    static const char int2hex[] = "0123456789abcdef";
    int i;
    int b;
    int ret;
    int hex_bytes;
    uint32_t cp;
    uint32_t state = 0;
    uint32_t c;
    uint8_t *p;
    struct flb_sds *head;

    head = FLB_SDS_HEADER(s);

    if (flb_sds_avail(s) <= str_len) {
        if ((s = flb_sds_increase(s, str_len)) == NULL) {
            return NULL;
        }
        head = FLB_SDS_HEADER(s);
    }

    for (i = 0; i < str_len; i++) {
        if (flb_sds_avail(s) < 6) {
            if ((s = flb_sds_increase(s, 6)) == NULL) {
                return NULL;
            }
            head = FLB_SDS_HEADER(s);
        }

        c = (uint32_t) str[i];
        if (c == '\\' || c == '"') {
            s[head->len++] = '\\';
            s[head->len++] = c;
        }
        else if (c >= '\b' && c <= '\r') {
            s[head->len++] = '\\';
            switch (c) {
            case '\n':
                s[head->len++] = 'n';
                break;
            case '\t':
                s[head->len++] = 't';
                break;
            case '\b':
                s[head->len++] = 'b';
                break;
            case '\f':
                s[head->len++] = 'f';
                break;
            case '\r':
                s[head->len++] = 'r';
                break;
            case '\v':
                s[head->len++] = 'u';
                s[head->len++] = '0';
                s[head->len++] = '0';
                s[head->len++] = '0';
                s[head->len++] = 'b';
                break;
            }
        }
        else if (c < 32 || c == 0x7f) {
            if (flb_sds_avail(s) < 6) {
                if ((s = flb_sds_increase(s, 6)) == NULL) {
                    return NULL;
                }
                head = FLB_SDS_HEADER(s);
            }
            s[head->len++] = '\\';
            s[head->len++] = 'u';
            s[head->len++] = '0';
            s[head->len++] = '0';
            s[head->len++] = int2hex[ (unsigned char) ((c & 0xf0) >> 4)];
            s[head->len++] = int2hex[ (unsigned char) (c & 0x0f)];
        }
        else if (c >= 0x80 && c <= 0xFFFF) {
            hex_bytes = flb_utf8_len(str + i);
            if (flb_sds_avail(s) < 6) {
                if ((s = flb_sds_increase(s, 2 + 6)) == NULL) {
                    return NULL;
                }
                head = FLB_SDS_HEADER(s);
            }

            state = FLB_UTF8_ACCEPT;
            cp = 0;
            for (b = 0; b < hex_bytes; b++) {
                p = (unsigned char *) str + i + b;
                ret = flb_utf8_decode(&state, &cp, *p);
                if (ret == 0) {
                    break;
                }
            }

            if (state != FLB_UTF8_ACCEPT) {
                /* Invalid UTF-8 hex, just skip utf-8 bytes */
                break;
            }
            else {
                s[head->len++] = '\\';
                s[head->len++] = 'u';
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0xf000) >> 12)];
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0x0f00) >> 8)];
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0xf0) >> 4)];
                s[head->len++] = int2hex[ (unsigned char) (cp & 0x0f)];
            }
            i += (hex_bytes - 1);
        }
        else if (c > 0xFFFF) {
            hex_bytes = flb_utf8_len(str + i);
            if (flb_sds_avail(s) < 10) {
                if ((s = flb_sds_increase(s, 10)) == NULL) {
                    return NULL;
                }
                head = FLB_SDS_HEADER(s);
            }

            state = FLB_UTF8_ACCEPT;
            cp = 0;
            for (b = 0; b < hex_bytes; b++) {
                p = (unsigned char *) str + i + b;
                ret = flb_utf8_decode(&state, &cp, *p);
                if (ret == 0) {
                    break;
                }
            }

            if (state != FLB_UTF8_ACCEPT) {
                /* Invalid UTF-8 hex, just skip utf-8 bytes */
                flb_warn("[pack] invalid UTF-8 bytes, skipping");
                break;
            }
            else {
                s[head->len++] = '\\';
                s[head->len++] = 'u';
                s[head->len++] = '0';
                s[head->len++] = '0';
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0xf00000) >> 20)];
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0x0f0000) >> 16)];
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0xf000) >> 12)];
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0x0f00) >> 8)];
                s[head->len++] = int2hex[ (unsigned char) ((cp & 0xf0) >> 4)];
                s[head->len++] = int2hex[ (unsigned char) (cp & 0x0f)];
            }
            i += (hex_bytes - 1);
        }
        else {
            s[head->len++] = c;
        }
    }

    s[head->len] = '\0';

    return s;
}

void flb_sds_destroy(flb_sds_t s)
{
    struct flb_sds *head;

    if (!s) {
        return;
    }

    head = FLB_SDS_HEADER(s);
    flb_free(head);
}

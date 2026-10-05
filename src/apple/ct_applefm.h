/*
 * ct_applefm.h
 *
 * C ABI of the Apple Foundation Models bridge (src/apple/CtAppleFM.swift).
 * The application loads the bridge at runtime (dlopen of libct_applefm.dylib)
 * so that the same binary runs on Macs without Apple Intelligence.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* availability codes */
#define CT_APPLEFM_AVAILABLE              0
#define CT_APPLEFM_UNSUPPORTED            1  /* macOS older than 26 or the Mac is not eligible */
#define CT_APPLEFM_NOT_ENABLED            2  /* Apple Intelligence is off in System Settings */
#define CT_APPLEFM_MODEL_NOT_READY        3  /* the model is still downloading */
#define CT_APPLEFM_UNKNOWN               -1

/* generate() result codes */
#define CT_APPLEFM_OK                     0
#define CT_APPLEFM_ERROR                  1
#define CT_APPLEFM_CANCELLED              2

/* one piece of generated text (UTF-8, not NUL-free safe: use the length) */
typedef void (*ct_applefm_piece_cb)(const char* piece, size_t piece_len, void* user_data);
/* returns non zero when the generation must stop */
typedef int (*ct_applefm_cancel_cb)(void* user_data);

/* bridge version, for sanity checks */
int ct_applefm_version(void);

/* availability of the on device model; reason (optional) receives a human readable explanation */
int ct_applefm_availability(char* reason, size_t reason_len);

/* the context window of the on device model, in tokens */
int ct_applefm_context_size(void);

/* blocking generation; pieces are delivered on the calling thread or an internal one, never concurrently */
int ct_applefm_generate(const char* instructions,
                        const char* prompt,
                        int max_tokens,
                        double temperature,
                        ct_applefm_piece_cb on_piece,
                        ct_applefm_cancel_cb should_cancel,
                        void* user_data,
                        char* error,
                        size_t error_len);

#ifdef __cplusplus
}
#endif

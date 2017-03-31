/*
 * Copyright (c) 2015 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#ifndef __FIREHOSE_BUFFER_PRIVATE__
#define __FIREHOSE_BUFFER_PRIVATE__

#if OS_FIREHOSE_SPI
#include <os/base.h>
#include <os/base_private.h>
#include <dispatch/dispatch.h>

#define OS_FIREHOSE_SPI_VERSION 20160318

/*!
 * @group Firehose SPI
 * SPI intended for logd only
 * Layout of structs is subject to change without notice
 */

#define FIREHOSE_BUFFER_CHUNK_SIZE				4096ul
#define FIREHOSE_BUFFER_LIBTRACE_HEADER_SIZE	2048ul
#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT		16

typedef union {
	uint64_t fbc_atomic_pos;
#define FIREHOSE_BUFFER_POS_ENTRY_OFFS_INC		(1ULL <<  0)
#define FIREHOSE_BUFFER_POS_PRIVATE_OFFS_INC	(1ULL << 16)
#define FIREHOSE_BUFFER_POS_REFCNT_INC			(1ULL << 32)
#define FIREHOSE_BUFFER_POS_FULL_BIT			(1ULL << 56)
#define FIREHOSE_BUFFER_POS_USABLE_FOR_STREAM(pos, stream) \
		((((pos).fbc_atomic_pos >> 48) & 0x1ff) == (uint16_t)stream)
	struct {
		uint16_t fbc_next_entry_offs;
		uint16_t fbc_private_offs;
		uint8_t  fbc_refcnt;
		uint8_t  fbc_qos_bits;
		uint8_t  fbc_stream;
		uint8_t  fbc_flag_full : 1;
		uint8_t  fbc_flag_io : 1;
		uint8_t  _fbc_flag_unused : 6;
	};
} firehose_buffer_pos_u;

typedef struct firehose_buffer_chunk_s {
	uint8_t  fbc_start[0];
	firehose_buffer_pos_u volatile fbc_pos;
	uint64_t fbc_timestamp;
	uint8_t  fbc_data[FIREHOSE_BUFFER_CHUNK_SIZE
			- sizeof(firehose_buffer_pos_u)
			- sizeof(uint64_t)];
} __attribute__((aligned(8))) *firehose_buffer_chunk_t;

typedef struct firehose_buffer_range_s {
	uint16_t fbr_offset; // offset from the start of the buffer
	uint16_t fbr_length;
} *firehose_buffer_range_t;


#define __firehose_critical_region_enter()
#define __firehose_critical_region_leave()

OS_EXPORT
const uint32_t _firehose_spi_version;

OS_ALWAYS_INLINE
static inline const uint8_t *
_firehose_tracepoint_reader_init(firehose_buffer_chunk_t fbc,
		const uint8_t **endptr)
{
	const uint8_t *start = fbc->fbc_data;
	const uint8_t *end = fbc->fbc_start + fbc->fbc_pos.fbc_next_entry_offs;

	if (end > fbc->fbc_start + FIREHOSE_BUFFER_CHUNK_SIZE) {
		end = start;
	}
	*endptr = end;
	return start;
}

OS_ALWAYS_INLINE
static inline firehose_tracepoint_t
_firehose_tracepoint_reader_next(const uint8_t **ptr, const uint8_t *end)
{
	const uint16_t ft_size = offsetof(struct firehose_tracepoint_s, ft_data);
	firehose_tracepoint_t ft;

	do {
		ft = (firehose_tracepoint_t)*ptr;
		if (ft->ft_data >= end) {
			// reached the end
			return NULL;
		}
		if (!ft->ft_length) {
			// tracepoint write didn't even start
			return NULL;
		}
		if (ft->ft_length > end - ft->ft_data) {
			// invalid length
			return NULL;
		}
		*ptr += roundup(ft_size + ft->ft_length, 8);
		// test whether write of the tracepoint was finished
	} while (os_unlikely(ft->ft_id.ftid_value == 0));

	return ft;
}

#define firehose_tracepoint_foreach(ft, fbc) \
		for (const uint8_t *end, *p = _firehose_tracepoint_reader_init(fbc, &end); \
				((ft) = _firehose_tracepoint_reader_next(&p, end)); )

OS_ALWAYS_INLINE
static inline bool
firehose_buffer_range_validate(firehose_buffer_chunk_t fbc,
		firehose_tracepoint_t ft, firehose_buffer_range_t range)
{
	if (range->fbr_offset + range->fbr_length > FIREHOSE_BUFFER_CHUNK_SIZE) {
		return false;
	}
	if (fbc->fbc_start + range->fbr_offset < ft->ft_data + ft->ft_length) {
		return false;
	}
	return true;
}


#endif // OS_FIREHOSE_SPI

#endif // __FIREHOSE_BUFFER_PRIVATE__

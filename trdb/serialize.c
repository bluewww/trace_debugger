/*
 * trdb - Debugger Software for the PULP platform
 *
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "serialize.h"
#include "list.h"
#include "trace_debugger.h"
#include "utils.h"

/* TODO: move those static functions into utils.h */

static uint32_t p_branch_map_len(uint32_t branches)
{
    if (branches == 0) {
        return 31;
    } else if (branches <= 1) {
        return 1;
    } else if (branches <= 9) {
        return 9;
    } else if (branches <= 17) {
        return 17;
    } else if (branches <= 25) {
        return 25;
    } else if (branches <= 31) {
        return 31;
    }
    return -1;
}


static uint32_t p_sign_extendable_bits(uint32_t addr)
{
    int clz = __builtin_clz(addr);
    int clo = __builtin_clz(~addr);
    return clz > clo ? clz : clo;
}


static uint32_t p_sext32(uint32_t val, uint32_t bit)
{
    if (bit == 0)
        return 0;

    if (bit == 32)
        return val;

    int m = 1U << (bit - 1);

    val = val & ((1U << bit) - 1);
    return (val ^ m) - m;
}


/* pulp specific packet serialization */
int trdb_pulp_serialize_packet(struct trdb_ctx *c, struct tr_packet *packet,
                               size_t *bitcnt, uint8_t align, uint8_t bin[])
{
    if (align >= 8) {
        err(c, "bad alignment value: %" PRId8 "\n", align);
        return -1;
    }

    union trdb_pack data = {0};
    /* We put the number of bytes (without header) as the packet length.
     * The PULPPKTLEN, and both FORMATLEN are considered the header
     */
    uint32_t bits_without_header = packet->length - 2 * FORMATLEN;
    uint32_t byte_len =
        bits_without_header / 8 + (bits_without_header % 8 != 0);
    if (byte_len >= 16) { // TODO: replace with pow or mask
        err(c, "bad packet length\n");
        return -1;
    }

    switch (packet->format) {
    case F_BRANCH_FULL: {
        uint32_t len = p_branch_map_len(packet->branches);

        /* we need enough space to do the packing it in uint128 */
        assert(128 > PULPPKTLEN + FORMATLEN + MSGTYPELEN + 5 + 31 + XLEN);
        /* TODO: assert branch map to overfull */
        data.bits =
            byte_len | (packet->msg_type << PULPPKTLEN)
            | (packet->format << (PULPPKTLEN + MSGTYPELEN))
            | (packet->branches << (PULPPKTLEN + MSGTYPELEN + FORMATLEN));
        data.bits |= ((__uint128_t)packet->branch_map & MASK_FROM(len))
                     << (PULPPKTLEN + MSGTYPELEN + FORMATLEN + BRANCHLEN);

        *bitcnt = PULPPKTLEN + MSGTYPELEN + FORMATLEN + BRANCHLEN + len;

        /* if we have a full branch we don't necessarily need to emit address */
        if (packet->branches > 0) {
            data.bits |=
                ((__uint128_t)packet->address
                 << (PULPPKTLEN + MSGTYPELEN + FORMATLEN + BRANCHLEN + len));
            if (trdb_is_full_address(c))
                *bitcnt += XLEN;
            else
                *bitcnt += (XLEN - p_sign_extendable_bits(packet->address) + 1);
        }
        data.bits <<= align;
        memcpy(bin, data.bin,
               (*bitcnt + align) / 8 + ((*bitcnt + align) % 8 != 0));

        return 0;
    }

    case F_BRANCH_DIFF: {
        if (trdb_is_full_address(c)) {
            err(c, "F_BRANCH_DIFF packet encountered but full_address set\n");
            return -1;
        }
        uint32_t len = p_branch_map_len(packet->branches);

        /* we need enough space to do the packing it in uint128 */
        assert(128 > PULPPKTLEN + FORMATLEN + MSGTYPELEN + 5 + 31 + XLEN);
        /* TODO: assert branch map to overfull */
        data.bits =
            byte_len | (packet->msg_type << PULPPKTLEN)
            | (packet->format << (PULPPKTLEN + MSGTYPELEN))
            | (packet->branches << (PULPPKTLEN + MSGTYPELEN + FORMATLEN));
        data.bits |= ((__uint128_t)packet->branch_map & MASK_FROM(len))
                     << (PULPPKTLEN + MSGTYPELEN + FORMATLEN + BRANCHLEN);

        *bitcnt = PULPPKTLEN + MSGTYPELEN + FORMATLEN + BRANCHLEN + len;

        /* if we have a full branch we don't necessarily need to emit address */
        if (packet->branches > 0) {
            data.bits |=
                ((__uint128_t)packet->address
                 << (PULPPKTLEN + MSGTYPELEN + FORMATLEN + BRANCHLEN + len));
            *bitcnt += (XLEN - p_sign_extendable_bits(packet->address) + 1);
        }
        data.bits <<= align;
        memcpy(bin, data.bin,
               (*bitcnt + align) / 8 + ((*bitcnt + align) % 8 != 0));

        return 0;
    }
    case F_ADDR_ONLY:
        assert(128 > PULPPKTLEN + MSGTYPELEN + FORMATLEN + XLEN);
        data.bits = byte_len | (packet->msg_type << PULPPKTLEN)
                    | (packet->format << (PULPPKTLEN + MSGTYPELEN))
                    | ((__uint128_t)packet->address
                       << (PULPPKTLEN + MSGTYPELEN + FORMATLEN));

        *bitcnt = PULPPKTLEN + MSGTYPELEN + FORMATLEN;
        if (trdb_is_full_address(c))
            *bitcnt += XLEN;
        else
            *bitcnt += (XLEN - p_sign_extendable_bits(packet->address) + 1);

        data.bits <<= align;
        /* this cuts off superfluous bits */
        memcpy(bin, data.bin,
               (*bitcnt + align) / 8 + ((*bitcnt + align) % 8 != 0));
        return 0;

    case F_SYNC:
        assert(PRIVLEN == 3);
        /* check for enough space to the packing */
        assert(128 > PULPPKTLEN + 4 + PRIVLEN + 1 + XLEN + CAUSELEN + 1);
        /* TODO: for now we ignore the context field since we have
         * only one hart
         */

        /* common part to all sub formats */
        data.bits =
            byte_len | (packet->msg_type << PULPPKTLEN)
            | (packet->format << (PULPPKTLEN + MSGTYPELEN))
            | (packet->subformat << (PULPPKTLEN + MSGTYPELEN + FORMATLEN))
            | (packet->privilege << (PULPPKTLEN + MSGTYPELEN + 2 * FORMATLEN));
        *bitcnt = PULPPKTLEN + MSGTYPELEN + 2 * FORMATLEN + PRIVLEN;

        /* to reduce repetition */
        uint32_t suboffset = PULPPKTLEN + MSGTYPELEN + 2 * FORMATLEN + PRIVLEN;
        switch (packet->subformat) {
        case SF_START:
            data.bits |= ((__uint128_t)packet->branch << suboffset)
                         | ((__uint128_t)packet->address << (suboffset + 1));
            *bitcnt += 1 + XLEN;
            break;

        case SF_EXCEPTION:
            data.bits |=
                (packet->branch << suboffset)
                | ((__uint128_t)packet->address << (suboffset + 1))
                | ((__uint128_t)packet->ecause << (suboffset + 1 + XLEN))
                | ((__uint128_t)packet->interrupt
                   << (suboffset + 1 + XLEN + CAUSELEN));
            // going to be zero anyway in our case
            //  | ((__uint128_t)packet->tval
            //   << (PULPPKTLEN + 4 + PRIVLEN + 1 + XLEN + CAUSELEN + 1));
            *bitcnt += (1 + XLEN + CAUSELEN + 1);
            break;

        case SF_CONTEXT:
            /* TODO: we still ignore the context field */
            break;
        }

        data.bits <<= align;
        memcpy(bin, data.bin,
               (*bitcnt + align) / 8 + ((*bitcnt + align) % 8 != 0));
        return 0;
    }
    return -1;
}


int trdb_pulp_read_single_packet(struct trdb_ctx *c, FILE *fp,
                                 struct tr_packet *packet, uint32_t *bytes)
{
    uint8_t header = 0;
    union trdb_pack payload = {0};
    if (!fp) {
        err(c, "fp is null\n");
        return -EINVAL;
    }
    if (fread(&header, 1, 1, fp) != 1) {
        if (feof(fp)) {
            return -1;
        } else if (ferror(fp)) {
            err(c, "ferror: %s\n", strerror(errno));
            return -1;
        }
        return -1;
    }

    /* read packet length it bits (including header) */
    uint8_t len = (header & MASK_FROM(PULPPKTLEN)) * 8 + 8;
    payload.bin[0] = header;
    /* compute how many bytes that is */
    uint32_t byte_len = len / 8 + (len % 8 != 0 ? 1 : 0);
    /* we have to exclude the header byte */
    if (fread((payload.bin + 1), 1, byte_len - 1, fp) != byte_len - 1) {
        if (feof(fp)) {
            err(c, "incomplete packet read\n");
            return -1;
        } else if (ferror(fp)) {
            err(c, "ferror: %s\n", strerror(errno));
            return -1;
        }
        return -1;
    }
    /* since we succefully read a packet we can now set bytes */
    *bytes = byte_len;
    /* make sure we start from a good state */
    *packet = (struct tr_packet){0};

    /* approxmation in multiple of 8*/
    packet->length =
        (header & MASK_FROM(PULPPKTLEN)) * 8 + MSGTYPELEN + FORMATLEN;
    packet->msg_type = (payload.bits >>= PULPPKTLEN) & MASK_FROM(MSGTYPELEN);

    switch (packet->msg_type) {
    /* we are dealing with a regular trace packet */
    case W_TRACE:
        packet->format = (payload.bits >>= MSGTYPELEN) & MASK_FROM(FORMATLEN);
        payload.bits >>= FORMATLEN;

        uint32_t blen = 0;
        uint32_t lower_boundary = 0;

        switch (packet->format) {
        case F_BRANCH_FULL:
            packet->branches = payload.bits & MASK_FROM(BRANCHLEN);
            blen = p_branch_map_len(packet->branches);
            packet->branch_map = (payload.bits >>= BRANCHLEN) & MASK_FROM(blen);

            lower_boundary = MSGTYPELEN + FORMATLEN + BRANCHLEN + blen;

            if (trdb_is_full_address(c)) {
                packet->address = (payload.bits >>= blen) & MASK_FROM(XLEN);
            } else {
                packet->address = (payload.bits >>= blen) & MASK_FROM(XLEN);
                packet->address =
                    p_sext32(packet->address, packet->length - lower_boundary);
            }
            return 0;
        case F_BRANCH_DIFF:
            if (trdb_is_full_address(c)) {
                err(c,
                    "F_BRANCH_DIFF packet encountered but full_address set\n");
                return -1;
            }
            packet->branches = payload.bits & MASK_FROM(BRANCHLEN);
            blen = p_branch_map_len(packet->branches);
            packet->branch_map = (payload.bits >>= BRANCHLEN) & MASK_FROM(blen);
            lower_boundary = MSGTYPELEN + FORMATLEN + BRANCHLEN + blen;

            if (trdb_is_full_address(c)) {
                packet->address = (payload.bits >>= blen) & MASK_FROM(XLEN);
            } else {
                packet->address = (payload.bits >>= blen) & MASK_FROM(XLEN);
                packet->address =
                    p_sext32(packet->address, packet->length - lower_boundary);
            }

            return 0;
        case F_ADDR_ONLY:
            if (trdb_is_full_address(c)) {
                packet->address = payload.bits & MASK_FROM(XLEN);
            } else {
                packet->address = p_sext32(
                    payload.bits, packet->length - MSGTYPELEN - FORMATLEN);
            }
            return 0;
        case F_SYNC:
            packet->subformat = payload.bits & MASK_FROM(FORMATLEN);
            packet->privilege =
                (payload.bits >>= FORMATLEN) & MASK_FROM(PRIVLEN);
            if (packet->subformat == SF_CONTEXT) {
                err(c, "not implemented\n");
                return -1;
            }
            packet->branch = (payload.bits >>= PRIVLEN) & 1;
            packet->address = (payload.bits >>= 1) & MASK_FROM(XLEN);
            if (packet->subformat == SF_START)
                return 0;
            packet->ecause = (payload.bits >>= XLEN) & MASK_FROM(CAUSELEN);
            packet->interrupt = (payload.bits >>= CAUSELEN) & 1;
            if (packet->subformat == SF_EXCEPTION)
                return 0;
        }
        break;
    /* this is user defined payload, written through the APB */
    case W_SOFTWARE:
        packet->userdata = (payload.bits >> MSGTYPELEN) & MASK_FROM(XLEN);
        return 0;
    /* timer data */
    case W_TIMER:
        /*careful if TIMELEN=64*/
        packet->time = (payload.bits >> MSGTYPELEN) & MASK_FROM(TIMELEN);
        return 0;
    default:
        err(c, "unknown message type in packet\n");
        return -1;
        break;
    }
    return -1;
}


int trdb_pulp_read_all_packets(struct trdb_ctx *c, const char *path,
                               struct list_head *packet_list)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        err(c, "fopen: %s\n", strerror(errno));
        return -1;
    }
    uint32_t total_bytes_read = 0;
    struct tr_packet *packet = NULL;
    struct tr_packet tmp = {0};
    uint32_t bytes = 0;

    /* read the file and malloc entries into the given linked list head */
    while (trdb_pulp_read_single_packet(c, fp, &tmp, &bytes) != -1) {
        packet = malloc(sizeof(*packet));
        if (!packet) {
            err(c, "malloc: %s\n", strerror(errno));
            return -ENOMEM;
        }
        *packet = tmp;
        total_bytes_read += bytes;

        list_add(&packet->list, packet_list);
    }
    dbg(c, "total bytes read: %" PRIu32 "\n", total_bytes_read);
    return 0;
}


int trdb_pulp_write_single_packet(struct trdb_ctx *c, struct tr_packet *packet,
                                  FILE *fp)
{
    size_t bitcnt = 0;
    size_t bytecnt = 0;
    uint8_t bin[16] = {0};
    if (!fp) {
        err(c, "bad file pointer\n");
        return -1;
    }
    if (trdb_pulp_serialize_packet(c, packet, &bitcnt, 0, bin)) {
        err(c, "failed to serialize packet\n");
        return -1;
    }
    bytecnt = bitcnt / 8 + (bitcnt % 8 != 0);
    if (fwrite(bin, 1, bytecnt, fp) != bytecnt) {
        if (feof(fp)) {
            /* TODO: uhhh */
        } else if (ferror(fp)) {
            err(c, "ferror: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}


int trdb_write_packets(struct trdb_ctx *c, const char *path,
                       struct list_head *packet_list)
{
    int status = 0;
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        err(c, "fopen: %s\n", strerror(errno));
        status = -1;
        goto fail;
    }

    uint8_t bin[sizeof(struct tr_packet)] = {0};
    size_t bitcnt = 0;
    uint32_t alignment = 0;
    uint8_t carry = 0;
    size_t good = 0;
    size_t rest = 0;

    struct tr_packet *packet;
    /* TODO: do we need the rever version? I think we do*/
    list_for_each_entry(packet, packet_list, list)
    {
        if (trdb_pulp_serialize_packet(c, packet, &bitcnt, alignment, bin)) {
            status = -1;
            goto fail;
        }
        /* stitch two consecutive packets together */
        bin[0] |= carry;
        rest = (bitcnt + alignment) % 8;
        good = (bitcnt + alignment) / 8;

        /* write as many bytes as we can i.e. withouth the potentially
         * intersecting ones
         */
        if (fwrite(bin, 1, good, fp) != good) {
            err(c, "fwrite: %s\n", strerror(errno));
            status = -1;
            goto fail;
        }
        /* we keep that for the next packet */
        carry = bin[good] & MASK_FROM(rest);
        alignment = rest;
    }
    /* done, write remaining carry */
    if (!fwrite(&bin[good], 1, 1, fp)) {
        err(c, "fwrite: %s\n", strerror(errno));
        status = -1;
    }
fail:
    if (fp)
        fclose(fp);
    return status;
}
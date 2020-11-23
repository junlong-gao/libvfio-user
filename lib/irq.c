/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *          Swapnil Ingle <swapnil.ingle@nutanix.com>
 *          Felipe Franciosi <felipe@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

#include <errno.h>
#include <limits.h>
#include <sys/eventfd.h>

#include "irq.h"
#include "tran_sock.h"

#define LM2VFIO_IRQT(type) (type - 1)

static const char *
vfio_irq_idx_to_str(int index) {
    static const char *s[] = {
        [VFIO_PCI_INTX_IRQ_INDEX] = "INTx",
        [VFIO_PCI_MSI_IRQ_INDEX]  = "MSI",
        [VFIO_PCI_MSIX_IRQ_INDEX] = "MSI-X",
    };

    assert(index < LM_DEV_NUM_IRQS);

    return s[index];
}

static long
irqs_disable(lm_ctx_t *lm_ctx, uint32_t index)
{
    int *irq_efd = NULL;
    uint32_t i;

    assert(lm_ctx != NULL);
    assert(index < LM_DEV_NUM_IRQS);

    switch (index) {
    case VFIO_PCI_INTX_IRQ_INDEX:
    case VFIO_PCI_MSI_IRQ_INDEX:
    case VFIO_PCI_MSIX_IRQ_INDEX:
        lm_log(lm_ctx, LM_DBG, "disabling IRQ %s", vfio_irq_idx_to_str(index));
        lm_ctx->irqs.type = IRQ_NONE;
        for (i = 0; i < lm_ctx->irqs.max_ivs; i++) {
            if (lm_ctx->irqs.efds[i] >= 0) {
                if (close(lm_ctx->irqs.efds[i]) == -1) {
                    lm_log(lm_ctx, LM_DBG, "failed to close IRQ fd %d: %m",
                           lm_ctx->irqs.efds[i]);
                }
                lm_ctx->irqs.efds[i] = -1;
            }
        }
        return 0;
    case VFIO_PCI_ERR_IRQ_INDEX:
        irq_efd = &lm_ctx->irqs.err_efd;
        break;
    case VFIO_PCI_REQ_IRQ_INDEX:
        irq_efd = &lm_ctx->irqs.req_efd;
        break;
    }

    if (irq_efd != NULL) {
        if (*irq_efd != -1) {
            if (close(*irq_efd) == -1) {
                lm_log(lm_ctx, LM_DBG, "failed to close IRQ fd %d: %m",
                       *irq_efd);
            }
            *irq_efd = -1;
        }
        return 0;
    }

    lm_log(lm_ctx, LM_DBG, "failed to disable IRQs");
    return -EINVAL;
}

static int
irqs_set_data_none(lm_ctx_t *lm_ctx, struct vfio_irq_set *irq_set)
{
    int efd;
    __u32 i;
    long ret;
    eventfd_t val;

    for (i = irq_set->start; i < (irq_set->start + irq_set->count); i++) {
        efd = lm_ctx->irqs.efds[i];
        if (efd >= 0) {
            val = 1;
            ret = eventfd_write(efd, val);
            if (ret == -1) {
                lm_log(lm_ctx, LM_DBG, "IRQ: failed to set data to none: %m");
                return -errno;
            }
        }
    }

    return 0;
}

static int
irqs_set_data_bool(lm_ctx_t *lm_ctx, struct vfio_irq_set *irq_set, void *data)
{
    uint8_t *d8;
    int efd;
    __u32 i;
    long ret;
    eventfd_t val;

    assert(data != NULL);
    for (i = irq_set->start, d8 = data; i < (irq_set->start + irq_set->count);
         i++, d8++) {
        efd = lm_ctx->irqs.efds[i];
        if (efd >= 0 && *d8 == 1) {
            val = 1;
            ret = eventfd_write(efd, val);
            if (ret == -1) {
                lm_log(lm_ctx, LM_DBG, "IRQ: failed to set data to bool: %m");
                return -errno;
            }
        }
    }

    return 0;
}

static int
irqs_set_data_eventfd(lm_ctx_t *lm_ctx, struct vfio_irq_set *irq_set, void *data)
{
    int32_t *d32;
    int efd;
    __u32 i;

    assert(data != NULL);
    for (i = irq_set->start, d32 = data; i < (irq_set->start + irq_set->count);
         i++, d32++) {
        efd = lm_ctx->irqs.efds[i];
        if (efd >= 0) {
            if (close(efd) == -1) {
                lm_log(lm_ctx, LM_DBG, "failed to close IRQ fd %d: %m", efd);
            }

            lm_ctx->irqs.efds[i] = -1;
        }
        if (*d32 >= 0) {
            lm_ctx->irqs.efds[i] = *d32;
        }
        lm_log(lm_ctx, LM_DBG, "event fd[%d]=%d", i, lm_ctx->irqs.efds[i]);
    }

    return 0;
}

static long
irqs_trigger(lm_ctx_t *lm_ctx, struct vfio_irq_set *irq_set, void *data)
{
    int err = 0;

    assert(lm_ctx != NULL);
    assert(irq_set != NULL);

    if (irq_set->count == 0) {
        return irqs_disable(lm_ctx, irq_set->index);
    }

    lm_log(lm_ctx, LM_DBG, "setting IRQ %s flags=%#x",
           vfio_irq_idx_to_str(irq_set->index), irq_set->flags);

    switch (irq_set->flags & VFIO_IRQ_SET_DATA_TYPE_MASK) {
    case VFIO_IRQ_SET_DATA_NONE:
        err = irqs_set_data_none(lm_ctx, irq_set);
        break;
    case VFIO_IRQ_SET_DATA_BOOL:
        err = irqs_set_data_bool(lm_ctx, irq_set, data);
        break;
    case VFIO_IRQ_SET_DATA_EVENTFD:
        err = irqs_set_data_eventfd(lm_ctx, irq_set, data);
        break;
    }

    return err;
}

static long
dev_set_irqs_validate(lm_ctx_t *lm_ctx, struct vfio_irq_set *irq_set)
{
    uint32_t a_type, d_type;

    assert(lm_ctx != NULL);
    assert(irq_set != NULL);

    // Separate action and data types from flags.
    a_type = (irq_set->flags & VFIO_IRQ_SET_ACTION_TYPE_MASK);
    d_type = (irq_set->flags & VFIO_IRQ_SET_DATA_TYPE_MASK);

    // Ensure index is within bounds.
    if (irq_set->index >= LM_DEV_NUM_IRQS) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ index %d\n", irq_set->index);
        return -EINVAL;
    }

    /* TODO make each condition a function */

    // Only one of MASK/UNMASK/TRIGGER is valid.
    if ((a_type != VFIO_IRQ_SET_ACTION_MASK) &&
        (a_type != VFIO_IRQ_SET_ACTION_UNMASK) &&
        (a_type != VFIO_IRQ_SET_ACTION_TRIGGER)) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ action mask %d\n", a_type);
        return -EINVAL;
    }
    // Only one of NONE/BOOL/EVENTFD is valid.
    if ((d_type != VFIO_IRQ_SET_DATA_NONE) &&
        (d_type != VFIO_IRQ_SET_DATA_BOOL) &&
        (d_type != VFIO_IRQ_SET_DATA_EVENTFD)) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ data %d\n", d_type);
        return -EINVAL;
    }
    // Ensure irq_set's start and count are within bounds.
    if ((irq_set->start >= lm_ctx->irq_count[irq_set->index]) ||
        (irq_set->start + irq_set->count > lm_ctx->irq_count[irq_set->index])) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ start/count\n");
        return -EINVAL;
    }
    // Only TRIGGER is valid for ERR/REQ.
    if (((irq_set->index == VFIO_PCI_ERR_IRQ_INDEX) ||
         (irq_set->index == VFIO_PCI_REQ_IRQ_INDEX)) &&
        (a_type != VFIO_IRQ_SET_ACTION_TRIGGER)) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ trigger w/o ERR/REQ\n");
        return -EINVAL;
    }
    // count == 0 is only valid with ACTION_TRIGGER and DATA_NONE.
    if ((irq_set->count == 0) && ((a_type != VFIO_IRQ_SET_ACTION_TRIGGER) ||
                                  (d_type != VFIO_IRQ_SET_DATA_NONE))) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ count %d\n", irq_set->count);
        return -EINVAL;
    }
    // If IRQs are set, ensure index matches what's enabled for the device.
    if ((irq_set->count != 0) && (lm_ctx->irqs.type != IRQ_NONE) &&
        (irq_set->index != LM2VFIO_IRQT(lm_ctx->irqs.type))) {
        lm_log(lm_ctx, LM_DBG, "bad IRQ index\n");
        return -EINVAL;
    }

    return 0;
}

static long
dev_set_irqs(lm_ctx_t *lm_ctx, struct vfio_irq_set *irq_set, void *data)
{
    long ret;

    assert(lm_ctx != NULL);
    assert(irq_set != NULL);

    // Ensure irq_set is valid.
    ret = dev_set_irqs_validate(lm_ctx, irq_set);
    if (ret != 0) {
        return ret;
    }

    switch (irq_set->flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
    case VFIO_IRQ_SET_ACTION_MASK:     // fallthrough
    case VFIO_IRQ_SET_ACTION_UNMASK:
        // We're always edge-triggered without un/mask support.
        return 0;
    }

    return irqs_trigger(lm_ctx, irq_set, data);
}

static long
dev_get_irqinfo(lm_ctx_t *lm_ctx, struct vfio_irq_info *irq_info_in,
                struct vfio_irq_info *irq_info_out)
{
    assert(lm_ctx != NULL);
    assert(irq_info_in != NULL);
    assert(irq_info_out != NULL);

    // Ensure provided argsz is sufficiently big and index is within bounds.
    if ((irq_info_in->argsz < sizeof(struct vfio_irq_info)) ||
        (irq_info_in->index >= LM_DEV_NUM_IRQS)) {
        lm_log(lm_ctx, LM_DBG, "bad irq_info (size=%d index=%d)\n",
               irq_info_in->argsz, irq_info_in->index);
        return -EINVAL;
    }

    irq_info_out->count = lm_ctx->irq_count[irq_info_in->index];
    irq_info_out->flags = VFIO_IRQ_INFO_EVENTFD;

    return 0;
}

int
handle_device_get_irq_info(lm_ctx_t *lm_ctx, uint32_t size,
                           struct vfio_irq_info *irq_info_in,
                           struct vfio_irq_info *irq_info_out)
{
    assert(lm_ctx != NULL);
    assert(irq_info_in != NULL);
    assert(irq_info_out != NULL);

    if (size != sizeof *irq_info_in || size != irq_info_in->argsz) {
        return -EINVAL;
    }

    return dev_get_irqinfo(lm_ctx, irq_info_in, irq_info_out);
}

int
handle_device_set_irqs(lm_ctx_t *lm_ctx, uint32_t size,
                       int *fds, int nr_fds, struct vfio_irq_set *irq_set)
{
    void *data = NULL;

    assert(lm_ctx != NULL);
    assert(irq_set != NULL);

    if (size < sizeof *irq_set || size != irq_set->argsz) {
        return -EINVAL;
    }

    switch (irq_set->flags & VFIO_IRQ_SET_DATA_TYPE_MASK) {
        case VFIO_IRQ_SET_DATA_EVENTFD:
            data = fds;
            if (nr_fds != (int)irq_set->count) {
                return -EINVAL;
            }
            break;
        case VFIO_IRQ_SET_DATA_BOOL:
            data = irq_set + 1;
            break;
        default:
            // FIXME?
            return -EINVAL;
    }

    return dev_set_irqs(lm_ctx, irq_set, data);
}

static int validate_irq_subindex(lm_ctx_t *lm_ctx, uint32_t subindex)
{
    if (lm_ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((subindex >= lm_ctx->irqs.max_ivs)) {
        lm_log(lm_ctx, LM_ERR, "bad IRQ %d, max=%d\n", subindex,
               lm_ctx->irqs.max_ivs);
        /* FIXME should return -errno */
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int
lm_irq_trigger(lm_ctx_t *lm_ctx, uint32_t subindex)
{
    int ret;
    eventfd_t val = 1;

    ret = validate_irq_subindex(lm_ctx, subindex);
    if (ret < 0) {
        return ret;
    }

    if (lm_ctx->irqs.efds[subindex] == -1) {
        lm_log(lm_ctx, LM_ERR, "no fd for interrupt %d\n", subindex);
        /* FIXME should return -errno */
        errno = ENOENT;
        return -1;
    }

    return eventfd_write(lm_ctx->irqs.efds[subindex], val);
}

int
lm_irq_message(lm_ctx_t *lm_ctx, uint32_t subindex)
{
    int ret, msg_id = 1;
    struct vfio_user_irq_info irq_info;

    ret = validate_irq_subindex(lm_ctx, subindex);
    if (ret < 0) {
        return -1;
    }

    irq_info.subindex = subindex;
    ret = send_recv_vfio_user_msg(lm_ctx->conn_fd, msg_id,
                                  VFIO_USER_VM_INTERRUPT,
                                  &irq_info, sizeof irq_info,
                                  NULL, 0, NULL, NULL, 0);
    if (ret < 0) {
        /* FIXME should return -errno */
	    errno = -ret;
	    return -1;
    }

    return 0;
}


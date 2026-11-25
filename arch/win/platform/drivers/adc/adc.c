/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#include <mrX/libc/mr_string.h>

/* Driver ADC operations */
static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(MR_USE_DRIVER_ADC_STREAM)
static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config);
static mr_err_t adc_stream_stop(const mr_driver_t *drv);
static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);

static const mr_adc_ops_t __win_adc_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
#if defined(MR_USE_DRIVER_ADC_STREAM)
    .stream_start = adc_stream_start,
    .stream_stop = adc_stream_stop,
    .stream_is_busy = adc_stream_is_busy,
    .stream_offset = adc_stream_offset,
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
    .event_control = adc_event_control,
};

static mr_size_t adc_elem_bytes(mr_uint8_t resolution) {
    if (resolution <= 8U) {
        return 1U;
    }
    if (resolution <= 16U) {
        return 2U;
    }
    return 4U;
}

static mr_uint32_t adc_mask_value(mr_uint32_t raw, mr_uint8_t resolution) {
    if (resolution >= 32U) {
        return raw;
    }
    return raw & ((1U << resolution) - 1U);
}

static mr_size_t adc_count_channels(mr_uint32_t channels) {
    mr_size_t cnt = 0U;
    mr_uint32_t i;

    for (i = 0U; i < 32U; i++) {
        if (channels & (1U << i)) {
            cnt++;
        }
    }
    return cnt;
}

static mr_size_t adc_write_frame_locked(const mr_sim_adc_t *sim,
                                        mr_uint32_t channels,
                                        mr_uint8_t resolution, mr_uint8_t *dst) {
    mr_size_t elem = adc_elem_bytes(resolution);
    mr_size_t written = 0U;
    mr_uint32_t i;

    for (i = 0U; i < 32U; i++) {
        mr_uint32_t val;

        if (!(channels & (1U << i))) {
            continue;
        }
        val = adc_mask_value(sim->values[i], resolution);
        if (elem == 1U) {
            dst[written] = (mr_uint8_t)val;
        } else if (elem == 2U) {
            mr_uint16_t v16 = (mr_uint16_t)val;
            mr_memcpy(dst + written, &v16, sizeof(v16));
        } else {
            mr_memcpy(dst + written, &val, sizeof(val));
        }
        written += elem;
    }
    return written;
}

static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config) || (channel >= 32U)) {
        return -MR_EINVAL;
    }
    if ((config->ref > MR_ADC_REF_EXTERNAL_1)
        || (config->gain > MR_ADC_GAIN_128)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->configs[channel] = *config;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config) || (channel >= 32U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *config = sim->configs[channel];
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    mr_adc_data_t *ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    mr_size_t ch_cnt, elem, frame_bytes, frames, f, total;
    mr_uint8_t *dst;
    int state;

    if ((!sim) || (!ad_data) || (!seq)) {
        return -MR_EINVAL;
    }
    if ((seq->channels == 0U) || ((seq->channels & ~ad_data->exist_mask) != 0U)) {
        return -MR_EINVAL;
    }
    if (seq->size == 0U) {
        return 0;
    }
    if (!seq->buf) {
        return -MR_EINVAL;
    }

    ch_cnt = adc_count_channels(seq->channels);
    elem = adc_elem_bytes(seq->resolution);
    frame_bytes = ch_cnt * elem;
    if (seq->size < frame_bytes) {
        return -MR_EINVAL;
    }

    frames = seq->size / frame_bytes;
    dst = (mr_uint8_t *)seq->buf;
    total = 0U;

    state = mr_spinlock_lock_irqsave(&sim->lock);
    for (f = 0U; f < frames; f++) {
        total += adc_write_frame_locked(sim, seq->channels, seq->resolution,
                                        dst + total);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return (mr_ssize_t)total;
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    mr_adc_data_t *ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    mr_size_t frame_bytes;
    int state;

    if ((!sim) || (!ad_data) || (!seq) || (!config)) {
        return -MR_EINVAL;
    }
    if ((seq->channels == 0U) || ((seq->channels & ~ad_data->exist_mask) != 0U)
        || (!seq->buf) || (seq->size == 0U)) {
        return -MR_EINVAL;
    }
    frame_bytes
        = adc_count_channels(seq->channels) * adc_elem_bytes(seq->resolution);
    if ((seq->size < frame_bytes) || ((seq->size % frame_bytes) != 0U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (sim->stream_busy) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }
    sim->stream_seq = *seq;
    sim->stream_cfg = *config;
    sim->stream_offset = 0U;
    sim->stream_half_sent = MR_FALSE;
    sim->stream_busy = MR_TRUE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->stream_busy = MR_FALSE;
    sim->stream_offset = 0U;
    sim->stream_half_sent = MR_FALSE;
    sim->stream_seq.buf = MR_NULL;
    sim->stream_seq.size = 0U;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!busy)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *busy = sim->stream_busy;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!offset)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *offset = sim->stream_offset;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

mr_err_t mr_sim_adc_stream_step(mr_driver_t *drv) {
    mr_sim_adc_t *sim;
    mr_uint32_t events = 0U;
    mr_size_t written, half;
    int state;

    if (!drv) {
        return -MR_EINVAL;
    }
    sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (!sim->stream_busy || !sim->stream_seq.buf) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }

    written = adc_write_frame_locked(
        sim, sim->stream_seq.channels, sim->stream_seq.resolution,
        (mr_uint8_t *)sim->stream_seq.buf + sim->stream_offset);
    sim->stream_offset += written;

    half = sim->stream_seq.size / 2U;
    if ((half > 0U) && !sim->stream_half_sent && (sim->stream_offset >= half)) {
        sim->stream_half_sent = MR_TRUE;
        events |= MR_ADC_WAIT_STREAM_HALF;
    }
    if (sim->stream_offset >= sim->stream_seq.size) {
        events |= MR_ADC_WAIT_STREAM_FULL;
        if (sim->stream_cfg.circular) {
            sim->stream_offset = 0U;
            sim->stream_half_sent = MR_FALSE;
        } else {
            sim->stream_busy = MR_FALSE;
        }
    }
    events &= sim->event_mask;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if (events != 0U) {
        mr_adc_event_raise(drv, events);
    }
    return 0;
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    mr_sim_adc_t *sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (enable) {
        sim->event_mask |= events;
    } else {
        sim->event_mask &= ~events;
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

/* =========================== Simulation Helpers =========================== */

mr_err_t mr_sim_adc_init(mr_driver_t *drv, mr_sim_adc_t *sim,
                         mr_uint16_t ref_internal, mr_uint32_t exist_mask) {
    mr_err_t ret;

    if ((!drv) || (!sim)) {
        return -MR_EINVAL;
    }

    mr_memset(sim, 0, sizeof(*sim));
    mr_spinlock_init(&sim->lock);
    sim->data.ref_internal = (ref_internal != 0U) ? ref_internal : 3300U;
    sim->data.exist_mask = exist_mask;

    ret = mr_adc_init(drv, &sim->data, sim, (mr_adc_ops_t *)&__win_adc_ops);
    if (ret != 0) {
        return ret;
    }
    ret = mr_driver_setup(drv);
    if (ret != 0) {
        mr_adc_del(drv);
        return ret;
    }
    return 0;
}

mr_err_t mr_sim_adc_set(mr_driver_t *drv, mr_uint32_t channel,
                        mr_uint32_t raw) {
    mr_adc_data_t *ad_data;
    mr_sim_adc_t *sim;
    int state;

    if ((!drv) || (channel >= 32U)) {
        return -MR_EINVAL;
    }
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    if ((!ad_data) || (!sim) || !(ad_data->exist_mask & (1U << channel))) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->values[channel] = raw;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

mr_err_t mr_sim_adc_get(mr_driver_t *drv, mr_uint32_t channel,
                        mr_uint32_t *raw) {
    mr_adc_data_t *ad_data;
    mr_sim_adc_t *sim;
    int state;

    if ((!drv) || (!raw) || (channel >= 32U)) {
        return -MR_EINVAL;
    }
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    sim = (mr_sim_adc_t *)MR_DRIVER_PRIV(drv);
    if ((!ad_data) || (!sim) || !(ad_data->exist_mask & (1U << channel))) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *raw = sim->values[channel];
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

mr_err_t mr_sim_adc_set_millivolt(mr_driver_t *drv, mr_uint32_t channel,
                                  mr_int32_t mv, mr_uint8_t resolution) {
    mr_adc_data_t *ad_data;
    mr_uint64_t full_scale, raw;

    if ((!drv) || (resolution == 0U) || (resolution > 32U) || (mv < 0)) {
        return -MR_EINVAL;
    }
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    if ((!ad_data) || (ad_data->ref_internal == 0U)) {
        return -MR_EINVAL;
    }

    full_scale = 1ULL << resolution;
    raw = ((mr_uint64_t)mv * full_scale) / (mr_uint64_t)ad_data->ref_internal;
    if (raw >= full_scale) {
        raw = full_scale - 1ULL;
    }
    return mr_sim_adc_set(drv, channel, (mr_uint32_t)raw);
}

/* ============================ HardTree Install ============================ */
#define MR_HT_COMPAT                    win_adc
#if MR_HT_HAS_DRIVER()
#define WIN_HT_ADC_INSTALL(_id, _ops)                                          \
    static mr_sim_adc_t MR_CAT(__win_adc_priv_, _id) = {                       \
        .lock = MR_SPINLOCK_INIT(),                                            \
    };                                                                         \
    MR_HT_ADC_INSTALL(_id, &MR_CAT(__win_adc_priv_, _id), _ops)

MR_HT_DRIVER_FOREACH(WIN_HT_ADC_INSTALL, &__win_adc_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) */

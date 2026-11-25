/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>
#include <mrX/libc/mr_string.h>
#include <stdio.h>

/* Driver UART operations */
static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config);
static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config);
static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size);
static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size);
#if defined(MR_USE_DRIVER_UART_STREAM)
static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                     mr_size_t size,
                                     const mr_uart_rx_stream_config_t *config);
static mr_err_t uart_rx_stream_stop(const mr_driver_t *drv);
static mr_err_t uart_rx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t uart_rx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset);
static mr_err_t uart_tx_stream_start(const mr_driver_t *drv, const void *buf,
                                     mr_size_t size,
                                     const mr_uart_tx_stream_config_t *config);
static mr_err_t uart_tx_stream_stop(const mr_driver_t *drv);
static mr_err_t uart_tx_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable);

static const mr_uart_ops_t __win_uart_ops = {
    .configure = uart_configure,
    .configuration = uart_configuration,
    .read = uart_read,
    .write = uart_write,
#if defined(MR_USE_DRIVER_UART_STREAM)
    .rx_stream_start = uart_rx_stream_start,
    .rx_stream_stop = uart_rx_stream_stop,
    .rx_stream_is_busy = uart_rx_stream_is_busy,
    .rx_stream_offset = uart_rx_stream_offset,
    .tx_stream_start = uart_tx_stream_start,
    .tx_stream_stop = uart_tx_stream_stop,
    .tx_stream_is_busy = uart_tx_stream_is_busy,
    .tx_stream_offset = uart_tx_stream_offset,
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
    .event_control = uart_event_control,
};

#if defined(MR_USE_WIN_UART_TCP)
static volatile LONG __wsa_inited = 0;

static mr_err_t win_uart_wsa_ensure(void) {
    WSADATA wsa;

    if (InterlockedCompareExchange(&__wsa_inited, 1, 1) != 0) {
        return 0;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return -MR_EIO;
    }
    InterlockedExchange(&__wsa_inited, 1);
    return 0;
}
#endif /* defined(MR_USE_WIN_UART_TCP) */

static mr_size_t ring_push(mr_uint8_t *ring, mr_size_t cap, mr_size_t *head,
                           mr_size_t *count, const mr_uint8_t *src,
                           mr_size_t len) {
    mr_size_t i, space, to_copy;

    space = cap - *count;
    to_copy = (len < space) ? len : space;
    for (i = 0U; i < to_copy; i++) {
        ring[*head] = src[i];
        *head = (*head + 1U) % cap;
    }
    *count += to_copy;
    return to_copy;
}

static mr_size_t ring_pop(const mr_uint8_t *ring, mr_size_t cap,
                          mr_size_t *tail, mr_size_t *count, mr_uint8_t *dst,
                          mr_size_t len) {
    mr_size_t i, to_copy;

    to_copy = (len < *count) ? len : *count;
    for (i = 0U; i < to_copy; i++) {
        dst[i] = ring[*tail];
        *tail = (*tail + 1U) % cap;
    }
    *count -= to_copy;
    return to_copy;
}

static mr_size_t win_uart_ingest_rx(mr_sim_uart_t *sim, const mr_uint8_t *data,
                                    mr_size_t size) {
    mr_uint32_t raise_events = 0U;
    mr_size_t pushed = 0U;
    int state;

    if ((!sim) || (!data) || (size == 0U)) {
        return 0U;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
#if defined(MR_USE_DRIVER_UART_STREAM)
    if (sim->rx_stream_busy && sim->rx_stream_buf && (sim->rx_stream_size > 0U)) {
        mr_size_t half = sim->rx_stream_size / 2U;
        mr_size_t i;

        for (i = 0U; i < size; i++) {
            mr_size_t prev_pos = sim->rx_stream_pos;

            sim->rx_stream_buf[sim->rx_stream_pos++] = data[i];
            pushed++;
            if ((half > 0U) && (prev_pos < half)
                && (sim->rx_stream_pos >= half)) {
                raise_events |= MR_UART_WAIT_RX_STREAM_HALF;
            }
            if (sim->rx_stream_pos >= sim->rx_stream_size) {
                raise_events |= MR_UART_WAIT_RX_STREAM_FULL;
                if (sim->rx_stream_circular) {
                    sim->rx_stream_pos = 0U;
                } else {
                    sim->rx_stream_busy = MR_FALSE;
                    break;
                }
            }
        }
    } else
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
    {
        pushed = ring_push(sim->rx_buf, MR_CFG_WIN_UART_RX_BUF_SIZE,
                           &sim->rx_head, &sim->rx_count, data, size);
        if (pushed > 0U) {
            raise_events |= MR_UART_WAIT_RX_READY;
        }
    }
    raise_events &= sim->event_mask;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if ((raise_events != 0U) && sim->drv) {
        mr_uart_event_raise((mr_driver_t *)sim->drv, raise_events);
    }
    return pushed;
}

static DWORD WINAPI win_uart_stdio_thread(LPVOID param) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)param;
    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD file_type, avail, nread;
    mr_uint8_t chunk[256];

    if ((h_stdin == INVALID_HANDLE_VALUE) || (h_stdin == NULL)) {
        return 0;
    }

    file_type = GetFileType(h_stdin);
    while (InterlockedCompareExchange(&sim->running, 1, 1) != 0) {
        if (file_type == FILE_TYPE_PIPE) {
            avail = 0;
            if (!PeekNamedPipe(h_stdin, NULL, 0, NULL, &avail, NULL)) {
                break;
            }
            if (avail == 0) {
                Sleep(10);
                continue;
            }
            if (avail > sizeof(chunk)) {
                avail = (DWORD)sizeof(chunk);
            }
            if (!ReadFile(h_stdin, chunk, avail, &nread, NULL) || (nread == 0)) {
                break;
            }
            win_uart_ingest_rx(sim, chunk, (mr_size_t)nread);
        } else if (file_type == FILE_TYPE_CHAR) {
            DWORD wait_res = WaitForSingleObject(h_stdin, 20);
            INPUT_RECORD rec;
            DWORD events_read = 0;

            if (wait_res != WAIT_OBJECT_0) {
                continue;
            }
            if (!ReadConsoleInputA(h_stdin, &rec, 1, &events_read)
                || (events_read == 0)) {
                continue;
            }
            if ((rec.EventType == KEY_EVENT) && rec.Event.KeyEvent.bKeyDown
                && (rec.Event.KeyEvent.uChar.AsciiChar != 0)) {
                mr_uint8_t ch = (mr_uint8_t)rec.Event.KeyEvent.uChar.AsciiChar;
                win_uart_ingest_rx(sim, &ch, 1U);
            }
        } else {
            if (!ReadFile(h_stdin, chunk, (DWORD)sizeof(chunk), &nread, NULL)
                || (nread == 0)) {
                break;
            }
            win_uart_ingest_rx(sim, chunk, (mr_size_t)nread);
        }
    }
    return 0;
}

#if defined(MR_USE_WIN_UART_TCP)
static DWORD WINAPI win_uart_tcp_thread(LPVOID param) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)param;
    mr_uint8_t chunk[512];

    while (InterlockedCompareExchange(&sim->running, 1, 1) != 0) {
        fd_set read_fds;
        struct timeval tv;
        SOCKET listen_s, client_s;
        int state;
        int ready;

        state = mr_spinlock_lock_irqsave(&sim->lock);
        listen_s = sim->listen_sock;
        client_s = sim->client_sock;
        mr_spinlock_unlock_irqrestore(&sim->lock, state);

        if (listen_s == INVALID_SOCKET) {
            break;
        }

        FD_ZERO(&read_fds);
        FD_SET(listen_s, &read_fds);
        if (client_s != INVALID_SOCKET) {
            FD_SET(client_s, &read_fds);
        }

        tv.tv_sec = 0;
        tv.tv_usec = 20000;
        ready = select(0, &read_fds, NULL, NULL, &tv);
        if (ready <= 0) {
            continue;
        }

        if (FD_ISSET(listen_s, &read_fds)) {
            SOCKET accepted = accept(listen_s, NULL, NULL);
            if (accepted != INVALID_SOCKET) {
                BOOL nodelay = TRUE;
                SOCKET old_client;

                setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY,
                           (const char *)&nodelay, sizeof(nodelay));
                state = mr_spinlock_lock_irqsave(&sim->lock);
                old_client = sim->client_sock;
                sim->client_sock = accepted;
                mr_spinlock_unlock_irqrestore(&sim->lock, state);
                if (old_client != INVALID_SOCKET) {
                    closesocket(old_client);
                }
                client_s = accepted;
            }
        }

        if ((client_s != INVALID_SOCKET) && FD_ISSET(client_s, &read_fds)) {
            int n = recv(client_s, (char *)chunk, (int)sizeof(chunk), 0);
            if (n > 0) {
                win_uart_ingest_rx(sim, chunk, (mr_size_t)n);
            } else {
                state = mr_spinlock_lock_irqsave(&sim->lock);
                if (sim->client_sock == client_s) {
                    sim->client_sock = INVALID_SOCKET;
                }
                mr_spinlock_unlock_irqrestore(&sim->lock, state);
                closesocket(client_s);
            }
        }
    }
    return 0;
}

static mr_err_t win_uart_tcp_start(mr_sim_uart_t *sim) {
    struct sockaddr_in addr;
    const char *host;
    int addr_len;
    BOOL reuse = TRUE;
    SOCKET s;
    mr_err_t ret;

    ret = win_uart_wsa_ensure();
    if (ret != 0) {
        return ret;
    }

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return -MR_EIO;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    mr_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(sim->sim_cfg.port);
    host = (sim->sim_cfg.host && (sim->sim_cfg.host[0] != '\0'))
               ? sim->sim_cfg.host
               : "127.0.0.1";
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        closesocket(s);
        return -MR_EINVAL;
    }

    if ((bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
        || (listen(s, 4) == SOCKET_ERROR)) {
        closesocket(s);
        return -MR_EIO;
    }

    addr_len = sizeof(addr);
    if (getsockname(s, (struct sockaddr *)&addr, &addr_len) == 0) {
        sim->bound_port = ntohs(addr.sin_port);
    } else {
        sim->bound_port = sim->sim_cfg.port;
    }

    sim->listen_sock = s;
    sim->client_sock = INVALID_SOCKET;
    InterlockedExchange(&sim->running, 1);
    sim->thread = CreateThread(NULL, 0, win_uart_tcp_thread, sim, 0, NULL);
    if (!sim->thread) {
        InterlockedExchange(&sim->running, 0);
        closesocket(sim->listen_sock);
        sim->listen_sock = INVALID_SOCKET;
        sim->bound_port = 0U;
        return -MR_EIO;
    }
    return 0;
}
#endif /* defined(MR_USE_WIN_UART_TCP) */

static mr_err_t win_uart_backend_start(const mr_driver_t *drv,
                                       mr_sim_uart_t *sim) {
    sim->drv = drv;
    if (sim->started) {
        return 0;
    }

    if (sim->sim_cfg.mode == MR_SIM_UART_MODE_STDIO) {
        InterlockedExchange(&sim->running, 1);
        sim->thread
            = CreateThread(NULL, 0, win_uart_stdio_thread, sim, 0, NULL);
        if (!sim->thread) {
            InterlockedExchange(&sim->running, 0);
            return -MR_EIO;
        }
        sim->started = MR_TRUE;
        return 0;
    }

    if (sim->sim_cfg.mode == MR_SIM_UART_MODE_TCP) {
#if defined(MR_USE_WIN_UART_TCP)
        mr_err_t ret = win_uart_tcp_start(sim);
        if (ret == 0) {
            sim->started = MR_TRUE;
        }
        return ret;
#else
        return -MR_ENOSYS;
#endif /* defined(MR_USE_WIN_UART_TCP) */
    }

    if (sim->sim_cfg.mode == MR_SIM_UART_MODE_MEM) {
        sim->started = MR_TRUE;
        return 0;
    }

    return -MR_EINVAL;
}

static void win_uart_backend_stop(mr_sim_uart_t *sim) {
    if (!sim || !sim->started) {
        return;
    }

    InterlockedExchange(&sim->running, 0);
#if defined(MR_USE_WIN_UART_TCP)
    if (sim->sim_cfg.mode == MR_SIM_UART_MODE_TCP) {
        int state;
        SOCKET listen_s, client_s;

        state = mr_spinlock_lock_irqsave(&sim->lock);
        listen_s = sim->listen_sock;
        client_s = sim->client_sock;
        sim->listen_sock = INVALID_SOCKET;
        sim->client_sock = INVALID_SOCKET;
        sim->bound_port = 0U;
        mr_spinlock_unlock_irqrestore(&sim->lock, state);

        if (client_s != INVALID_SOCKET) {
            closesocket(client_s);
        }
        if (listen_s != INVALID_SOCKET) {
            closesocket(listen_s);
        }
    }
#endif /* defined(MR_USE_WIN_UART_TCP) */

    if (sim->thread) {
        WaitForSingleObject(sim->thread, 1000);
        CloseHandle(sim->thread);
        sim->thread = NULL;
    }
    sim->started = MR_FALSE;
}

static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;
    mr_err_t ret;

    if ((!sim) || (!config) || (config->baud_rate == 0U)) {
        return -MR_EINVAL;
    }

    ret = win_uart_backend_start(drv, sim);
    if (ret != 0) {
        return ret;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->uart_cfg = *config;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *config = sim->uart_cfg;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;
    mr_size_t n;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
#if defined(MR_USE_DRIVER_UART_STREAM)
    if (sim->rx_stream_busy) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
    n = ring_pop(sim->rx_buf, MR_CFG_WIN_UART_RX_BUF_SIZE, &sim->rx_tail,
                 &sim->rx_count, (mr_uint8_t *)buf, size);
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return (mr_ssize_t)n;
}

static mr_ssize_t win_uart_write_raw(const mr_driver_t *drv, mr_sim_uart_t *sim,
                                    const void *buf, mr_size_t size,
                                    mr_bool_t whole) {
    mr_uint32_t events = 0U;
    mr_ssize_t written = 0;
    int state;

    if (sim->sim_cfg.mode == MR_SIM_UART_MODE_STDIO) {
        size_t n = fwrite(buf, 1U, (size_t)size, stdout);
        fflush(stdout);
        if ((n != (size_t)size) && ferror(stdout)) {
            return -MR_EIO;
        }
        written = (mr_ssize_t)n;
    } else if (sim->sim_cfg.mode == MR_SIM_UART_MODE_MEM) {
        if (whole && (size > MR_CFG_WIN_UART_RX_BUF_SIZE)) {
            return -MR_ERANGE;
        }

        /* Keep stream writes atomic in the TX ring */
        state = mr_spinlock_lock_irqsave(&sim->lock);
        if (whole && (size > (MR_CFG_WIN_UART_RX_BUF_SIZE - sim->tx_count))) {
            mr_spinlock_unlock_irqrestore(&sim->lock, state);
            return -MR_EAGAIN;
        }
        written = (mr_ssize_t)ring_push(sim->tx_buf, MR_CFG_WIN_UART_RX_BUF_SIZE,
                                        &sim->tx_head, &sim->tx_count,
                                        (const mr_uint8_t *)buf, size);
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
    }
#if defined(MR_USE_WIN_UART_TCP)
    else if (sim->sim_cfg.mode == MR_SIM_UART_MODE_TCP) {
        SOCKET client_s;
        const char *p = (const char *)buf;
        mr_size_t total = 0U;

        state = mr_spinlock_lock_irqsave(&sim->lock);
        client_s = sim->client_sock;
        mr_spinlock_unlock_irqrestore(&sim->lock, state);

        if (client_s == INVALID_SOCKET) {
            return 0;
        }

        while (total < size) {
            int n = send(client_s, p + total, (int)(size - total), 0);
            if (n <= 0) {
                state = mr_spinlock_lock_irqsave(&sim->lock);
                if (sim->client_sock == client_s) {
                    sim->client_sock = INVALID_SOCKET;
                }
                mr_spinlock_unlock_irqrestore(&sim->lock, state);
                closesocket(client_s);
                return (total > 0U) ? (mr_ssize_t)total : -MR_EIO;
            }
            total += (mr_size_t)n;
        }
        written = (mr_ssize_t)total;
    }
#endif /* defined(MR_USE_WIN_UART_TCP) */
    else {
        return -MR_ENOSYS;
    }

    if (written > 0) {
        state = mr_spinlock_lock_irqsave(&sim->lock);
        events = sim->event_mask & MR_UART_WAIT_TX_DONE;
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        if (events != 0U) {
            mr_uart_event_raise((mr_driver_t *)drv, events);
        }
    }
    return written;
}

static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    if (!sim->started) {
        mr_err_t ret = win_uart_backend_start(drv, sim);
        if (ret != 0) {
            return ret;
        }
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
#if defined(MR_USE_DRIVER_UART_STREAM)
    if (sim->tx_stream_busy) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    return win_uart_write_raw(drv, sim, buf, size, MR_FALSE);
}

#if defined(MR_USE_DRIVER_UART_STREAM)
static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                     mr_size_t size,
                                     const mr_uart_rx_stream_config_t *config) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (sim->rx_stream_busy) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }
    sim->rx_stream_buf = (mr_uint8_t *)buf;
    sim->rx_stream_size = size;
    sim->rx_stream_pos = 0U;
    sim->rx_stream_circular = config ? config->circular : MR_FALSE;
    sim->rx_stream_busy = MR_TRUE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_rx_stream_stop(const mr_driver_t *drv) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->rx_stream_busy = MR_FALSE;
    sim->rx_stream_buf = MR_NULL;
    sim->rx_stream_size = 0U;
    sim->rx_stream_pos = 0U;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_rx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *busy = sim->rx_stream_busy;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_rx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *offset = sim->rx_stream_pos;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_tx_stream_start(const mr_driver_t *drv, const void *buf,
                                     mr_size_t size,
                                     const mr_uart_tx_stream_config_t *config) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    mr_uint32_t events;
    mr_ssize_t written;
    mr_err_t ret;
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (sim->tx_stream_busy) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }
    sim->tx_stream_buf = (const mr_uint8_t *)buf;
    sim->tx_stream_size = size;
    sim->tx_stream_pos = 0U;
    sim->tx_stream_circular = config ? config->circular : MR_FALSE;
    sim->tx_stream_busy = MR_TRUE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    written = win_uart_write_raw(drv, sim, buf, size, MR_TRUE);
    if (written < 0) {
        ret = (mr_err_t)written;
    } else {
        ret = ((mr_size_t)written == size) ? 0 : -MR_EIO;
    }

    /* Commit progress before reporting completion */
    events = 0U;
    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (written >= 0) {
        sim->tx_stream_pos = (mr_size_t)written;
    }
    sim->tx_stream_busy = (ret == 0) && sim->tx_stream_circular;
    if (ret == 0) {
        events = sim->event_mask
                 & (MR_UART_WAIT_TX_STREAM_HALF | MR_UART_WAIT_TX_STREAM_DONE);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if (events != 0U) {
        mr_uart_event_raise((mr_driver_t *)drv, events);
    }
    return ret;
}

static mr_err_t uart_tx_stream_stop(const mr_driver_t *drv) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->tx_stream_busy = MR_FALSE;
    sim->tx_stream_buf = MR_NULL;
    sim->tx_stream_size = 0U;
    sim->tx_stream_pos = 0U;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_tx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *busy = sim->tx_stream_busy;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *offset = sim->tx_stream_pos;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */

static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable) {
    mr_sim_uart_t *sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
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

mr_err_t mr_sim_uart_init(mr_driver_t *drv, mr_sim_uart_t *sim,
                          const mr_sim_uart_config_t *cfg) {
    mr_err_t ret;

    if ((!drv) || (!sim)) {
        return -MR_EINVAL;
    }

    mr_memset(sim, 0, sizeof(*sim));
    mr_spinlock_init(&sim->lock);
    sim->uart_cfg.baud_rate = 115200U;
    sim->uart_cfg.data_bits = MR_UART_DATA_BITS_8;
    sim->uart_cfg.stop_bits = MR_UART_STOP_BITS_1;
    sim->uart_cfg.parity = MR_UART_PARITY_NONE;
    sim->uart_cfg.flow_ctrl = MR_UART_FLOW_CTRL_NONE;
    sim->uart_cfg.mode = MR_UART_MODE_TX_RX;
#if defined(MR_USE_WIN_UART_TCP)
    sim->listen_sock = INVALID_SOCKET;
    sim->client_sock = INVALID_SOCKET;
#endif /* defined(MR_USE_WIN_UART_TCP) */
    if (cfg) {
        sim->sim_cfg = *cfg;
    } else {
        sim->sim_cfg.mode = MR_SIM_UART_MODE_STDIO;
    }

    ret = mr_uart_init(drv, &sim->data, sim, (mr_uart_ops_t *)&__win_uart_ops);
    if (ret != 0) {
        return ret;
    }
    ret = mr_driver_setup(drv);
    if (ret != 0) {
        mr_uart_del(drv);
        return ret;
    }
    ret = win_uart_backend_start(drv, sim);
    if (ret != 0) {
        mr_driver_shutdown(drv);
        mr_uart_del(drv);
        return ret;
    }
    return 0;
}

mr_err_t mr_sim_uart_deinit(mr_driver_t *drv) {
    mr_sim_uart_t *sim;

    if (!drv) {
        return -MR_EINVAL;
    }

    sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    if (sim) {
        win_uart_backend_stop(sim);
    }
    mr_driver_shutdown(drv);
    return mr_uart_del(drv);
}

mr_ssize_t mr_sim_uart_rx_push(mr_driver_t *drv, const void *buf,
                               mr_size_t size) {
    mr_sim_uart_t *sim;

    if ((!drv) || ((!buf) && (size != 0U))) {
        return -MR_EINVAL;
    }

    sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }
    sim->drv = drv;
    return (mr_ssize_t)win_uart_ingest_rx(sim, (const mr_uint8_t *)buf, size);
}

mr_ssize_t mr_sim_uart_tx_pop(mr_driver_t *drv, void *buf, mr_size_t size) {
    mr_sim_uart_t *sim;
    int state;
    mr_size_t n;

    if ((!drv) || ((!buf) && (size != 0U))) {
        return -MR_EINVAL;
    }

    sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    n = ring_pop(sim->tx_buf, MR_CFG_WIN_UART_RX_BUF_SIZE, &sim->tx_tail,
                 &sim->tx_count, (mr_uint8_t *)buf, size);
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return (mr_ssize_t)n;
}

#if defined(MR_USE_WIN_UART_TCP)
mr_uint16_t mr_sim_uart_tcp_port(mr_driver_t *drv) {
    mr_sim_uart_t *sim;

    if (!drv) {
        return 0U;
    }
    sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    return sim ? sim->bound_port : 0U;
}

mr_bool_t mr_sim_uart_tcp_connected(mr_driver_t *drv) {
    mr_sim_uart_t *sim;
    int state;
    mr_bool_t connected;

    if (!drv) {
        return MR_FALSE;
    }
    sim = (mr_sim_uart_t *)MR_DRIVER_PRIV(drv);
    if (!sim || !sim->started) {
        return MR_FALSE;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    connected = (sim->client_sock != INVALID_SOCKET) ? MR_TRUE : MR_FALSE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return connected;
}
#endif /* defined(MR_USE_WIN_UART_TCP) */

/* ============================ HardTree Install ============================ */
#define MR_HT_COMPAT                    win_uart
#if MR_HT_HAS_DRIVER()
#define WIN_HT_UART_MODE(_id)                                                  \
    MR_HT_DRIVER_PROP_OR(                                                      \
        _id, win_mode,                                                         \
        ((MR_HT_DRIVER_PROP_OR(_id, win_port, 0U) > 0U)                        \
             ? MR_SIM_UART_MODE_TCP                                            \
             : MR_SIM_UART_MODE_STDIO))

#define WIN_HT_UART_INSTALL(_id, _ops)                                         \
    static mr_sim_uart_t MR_CAT(__win_uart_priv_, _id) = {                     \
        .uart_cfg = {                                                          \
            .baud_rate = 115200U,                                              \
            .data_bits = MR_UART_DATA_BITS_8,                                  \
            .stop_bits = MR_UART_STOP_BITS_1,                                  \
            .parity = MR_UART_PARITY_NONE,                                     \
            .flow_ctrl = MR_UART_FLOW_CTRL_NONE,                               \
            .mode = MR_UART_MODE_TX_RX,                                        \
        },                                                                     \
        .sim_cfg = {                                                           \
            .mode = WIN_HT_UART_MODE(_id),                                     \
            .host = MR_HT_DRIVER_PROP_OR(_id, win_host, "127.0.0.1"),          \
            .port = MR_HT_DRIVER_PROP_OR(_id, win_port, 0U),                   \
        },                                                                     \
        .lock = MR_SPINLOCK_INIT(),                                            \
    };                                                                         \
    MR_HT_UART_INSTALL(_id, &MR_CAT(__win_uart_priv_, _id), _ops)

MR_HT_DRIVER_FOREACH(WIN_HT_UART_INSTALL, &__win_uart_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_UART) */

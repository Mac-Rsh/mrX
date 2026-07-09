/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/ram/mr_ram.h>
#if defined(MR_USE_DRIVER_RAM)
#include <mrX/libc/mr_malloc.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    ram_heaps
#if MR_HT_HAS_DRIVER()

/* Driver RAM operations */
static mr_err_t ram_heap_mount(const mr_driver_t *drv);
static mr_err_t ram_heap_unmount(const mr_driver_t *drv);
MR_UNUSED static const mr_ram_ops_t __drv_ram_ops
    = {.mount = ram_heap_mount, .unmount = ram_heap_unmount};

static mr_err_t ram_heap_mount(const mr_driver_t *drv) {
    mr_ram_data_t *data = MR_DRIVER_DATA(drv);
    mr_heap_region_t regions[2];
    mr_uint32_t i;

    /* Mount RAM regions to heap */
    for (i = 0; i < data->regions_num; i++) {
        regions[0].address = data->regions[i].address;
        regions[0].size = data->regions[i].size;
        regions[1].address = MR_NULL;
        mr_heap_region_add(regions);
    }
    return 0;
}

static mr_err_t ram_heap_unmount(const mr_driver_t *drv) {
    MR_IGNORE(drv);

    /* Can't unmount RAM */
    return -MR_EIO;
}

/* Instance install */
#define RAM_HEAP_HT_RAM_INSTALL(_id, _ops)                                     \
    MR_HT_RAM_INSTALL(_id, MR_NULL, _ops)
MR_HT_DRIVER_FOREACH(RAM_HEAP_HT_RAM_INSTALL, &__drv_ram_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_RAM) */

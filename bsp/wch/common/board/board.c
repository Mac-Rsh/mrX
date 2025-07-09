/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-06-11        MacRsh        First version
 */

#include <board/mr_board.h>

void mr_board_init(void) {
    /* Init systick */
    mr_board_systick_init();
}

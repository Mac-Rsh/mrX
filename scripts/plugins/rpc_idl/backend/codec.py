#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Codec - Compact/scatter pack and unpack emission.

Renders in-place compact-to-scatter unpack and scatter-to-compact pack
helpers from a payload layout.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..semantic.model import Method, Service
from .layout import Layout, Slot, VarSlot
from .names import CNames
from .emit import CEmit


class CodecBuilder:
    """Renders unpack and pack helpers for one method payload."""

    def unpack_lines(self, service: Service, method: Method, which: str,
                     layout: Layout) -> list[str]:
        """Emit one compact-to-scatter unpack function."""
        typedef = CNames.payload_typedef(service.name, method.name, which)
        name = CNames.unpack_fn(service.name, method.name, which)
        n_count = layout.var_count
        src_count = len(layout.moving_slots)
        indent = CEmit.INDENT
        lines = [
            f"static mr_err_t {name}(",
            f"{indent}{typedef} *{which}, mr_uint32_t compact_size) {{",
        ]
        lines.extend(self._locals(n_count, src_count, has_p=True))
        lines.append("")
        lines.append(f"{indent}p = (mr_uint8_t *){which};")
        lines.append(f"{indent}off = 0U;")

        moves: list[tuple[Slot, int, int | None]] = []
        seen_var = False
        var_i = 0
        src_i = 0
        for slot in layout.slots:
            n_expr = CEmit.arr("n", var_i, n_count) \
                if isinstance(slot, VarSlot) else None
            src_expr = CEmit.arr("src", src_i, src_count) if seen_var else None
            lines.extend(self._unpack_measure(slot, which, n_expr, src_expr))
            if seen_var:
                n_index = var_i if isinstance(slot, VarSlot) else None
                moves.append((slot, src_i, n_index))
                src_i += 1
            if isinstance(slot, VarSlot):
                var_i += 1
                seen_var = True
        lines.extend([
            f"{indent}if (off != compact_size) {{",
            f"{indent * 2}return -MR_EINVAL;",
            f"{indent}}}",
        ])
        if moves:
            lines.append("")
            for slot, src_index, n_index in reversed(moves):
                src_expr = CEmit.arr("src", src_index, src_count)
                n_expr = None if n_index is None \
                    else CEmit.arr("n", n_index, n_count)
                lines.extend(self._unpack_move(slot, which, src_expr, n_expr))
        # Restore native counts only after all compact source bytes have moved.
        # The wire prefix is little-endian even on a big-endian target.
        var_i = 0
        for slot in layout.slots:
            if isinstance(slot, VarSlot):
                if slot.prefix_size > 1:
                    access = self._access(which, slot)
                    n_expr = CEmit.arr("n", var_i, n_count)
                    size_c = CNames.SCALARS[slot.size_type]
                    lines.append(
                        f"{indent}{access}.size = ({size_c}){n_expr};")
                var_i += 1
        lines.append(f"{indent}return 0;")
        lines.append("}")
        return lines

    def pack_lines(self, service: Service, method: Method, which: str,
                   layout: Layout) -> list[str]:
        """Emit one scatter-to-compact pack function."""
        typedef = CNames.payload_typedef(service.name, method.name, which)
        name = CNames.pack_fn(service.name, method.name, which)
        n_count = layout.var_count
        moving = set(layout.moving_slots)
        indent = CEmit.INDENT
        lines = [
            f"static mr_err_t {name}(",
            f"{indent}{typedef} *{which}, mr_uint32_t *compact_size) {{",
        ]
        needs_pointer = bool(moving) or any(
            isinstance(slot, VarSlot) and slot.prefix_size > 1
            for slot in layout.slots)
        lines.extend(self._locals(n_count, 0, has_p=needs_pointer))
        lines.append("")
        if needs_pointer:
            lines.append(f"{indent}p = (mr_uint8_t *){which};")
        lines.append(f"{indent}off = 0U;")

        var_i = 0
        for slot in layout.slots:
            n_expr = CEmit.arr("n", var_i, n_count) \
                if isinstance(slot, VarSlot) else None
            lines.extend(self._pack_slot(slot, which, slot in moving, n_expr))
            if isinstance(slot, VarSlot):
                var_i += 1
        lines.append(f"{indent}*compact_size = off;")
        lines.append(f"{indent}return 0;")
        lines.append("}")
        return lines

    @staticmethod
    def _locals(n_count: int, src_count: int, has_p: bool) -> list[str]:
        """Declare locals in `src`, `n`, `off`, `p` order."""
        indent = CEmit.INDENT
        lines: list[str] = []
        if src_count:
            lines.append(
                f"{indent}{CEmit.arr_decl('mr_uint32_t', 'src', src_count)}")
        if n_count:
            lines.append(
                f"{indent}{CEmit.arr_decl('mr_uint32_t', 'n', n_count)}")
        lines.append(f"{indent}mr_uint32_t off;")
        if has_p:
            lines.append(f"{indent}mr_uint8_t *p;")
        return lines

    def _unpack_measure(self, slot: Slot, param: str, n_expr: str | None,
                        src_expr: str | None) -> list[str]:
        """Validate one compact slot and optionally remember its wire start."""
        access = self._access(param, slot)
        indent = CEmit.INDENT
        lines: list[str] = []
        if isinstance(slot, VarSlot):
            prefix = f"{slot.prefix_size}U"
            n_expr = n_expr or "n"
            data_size = self._data_size(slot, n_expr)
            lines.extend([
                f"{indent}if ((off + {prefix}) > compact_size) {{",
                f"{indent * 2}return -MR_EINVAL;",
                f"{indent}}}",
            ])
            lines.extend(self._load_size(slot, n_expr))
            lines.extend([
                f"{indent}if (({n_expr} > {slot.max_count}U) || "
                f"((off + {prefix} + {data_size}) > compact_size)) {{",
                f"{indent * 2}return -MR_EINVAL;",
                f"{indent}}}",
            ])
            if src_expr is not None:
                lines.append(f"{indent}{src_expr} = off;")
            lines.append(f"{indent}off += {prefix} + {data_size};")
            return lines
        size = f"sizeof({access})"
        lines.extend([
            f"{indent}if ((off + {size}) > compact_size) {{",
            f"{indent * 2}return -MR_EINVAL;",
            f"{indent}}}",
        ])
        if src_expr is not None:
            lines.append(f"{indent}{src_expr} = off;")
        lines.append(f"{indent}off += {size};")
        return lines

    def _unpack_move(self, slot: Slot, param: str, src_expr: str,
                     n_expr: str | None) -> list[str]:
        """Move one compact slot into its scatter field."""
        access = self._access(param, slot)
        if isinstance(slot, VarSlot):
            data_size = self._data_size(slot, n_expr or "n")
            length = f"{slot.prefix_size}U + {data_size}"
        else:
            length = f"sizeof({access})"
        return self._memmove(f"&{access}", f"p + {src_expr}", length)

    def _pack_slot(self, slot: Slot, param: str, move: bool,
                   n_expr: str | None) -> list[str]:
        """Emit scatter-to-compact movement for one slot."""
        access = self._access(param, slot)
        indent = CEmit.INDENT
        lines: list[str] = []
        if isinstance(slot, VarSlot):
            n_expr = n_expr or "n"
            lines.extend([
                f"{indent}{n_expr} = {access}.size;",
                f"{indent}if ({n_expr} > {slot.max_count}U) {{",
                f"{indent * 2}return -MR_EINVAL;",
                f"{indent}}}",
            ])
            data_size = self._data_size(slot, n_expr)
            length = f"{slot.prefix_size}U + {data_size}"
            if move:
                lines.extend(self._memmove("p + off", f"&{access}", length))
            if slot.prefix_size > 1:
                # Movement copies native .size bytes. Write the actual wire
                # prefix explicitly without changing the element byte order.
                for byte in range(slot.prefix_size):
                    target = "p[off]" if byte == 0 else f"p[off + {byte}U]"
                    value = n_expr if byte == 0 else f"({n_expr} >> {byte * 8}U)"
                    lines.append(f"{indent}{target} = (mr_uint8_t){value};")
            lines.append(f"{indent}off += {length};")
            return lines
        length = f"sizeof({access})"
        if move:
            lines.extend(self._memmove("p + off", f"&{access}", length))
        lines.append(f"{indent}off += {length};")
        return lines

    @staticmethod
    def _data_size(slot: VarSlot, count: str) -> str:
        """Return the compact byte count for `count` array elements."""
        if slot.element_size == 1:
            return count
        return f"({count} * {slot.element_size}U)"

    def _load_size(self, slot: VarSlot, dest: str) -> list[str]:
        """Load a little-endian size prefix at `p[off]` into `dest`."""
        indent = CEmit.INDENT
        if slot.size_type == "u8":
            return [f"{indent}{dest} = p[off];"]
        if slot.size_type == "u16":
            return [
                f"{indent}{dest} = (mr_uint32_t)p[off];",
                f"{indent}{dest} |= (mr_uint32_t)p[off + 1U] << 8U;",
            ]
        return [
            f"{indent}{dest} = (mr_uint32_t)p[off];",
            f"{indent}{dest} |= (mr_uint32_t)p[off + 1U] << 8U;",
            f"{indent}{dest} |= (mr_uint32_t)p[off + 2U] << 16U;",
            f"{indent}{dest} |= (mr_uint32_t)p[off + 3U] << 24U;",
        ]

    def _memmove(self, dst: str, src: str, size: str) -> list[str]:
        """Format one `mr_memmove`, wrapping when the line is too long."""
        indent = CEmit.INDENT
        joined = f"{indent}mr_memmove({dst}, {src}, {size});"
        if len(joined) <= CEmit.LINE_WIDTH:
            return [joined]
        return [
            f"{indent}mr_memmove({dst},",
            f"{indent * 2}{src}, {size});",
        ]

    @staticmethod
    def _access(param: str, slot: Slot) -> str:
        """Return a `param->field` expression for one slot."""
        return f"{param}->{slot.path}"

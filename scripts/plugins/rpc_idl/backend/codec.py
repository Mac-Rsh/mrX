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
from .layout import FixedSlot, Layout, Slot, VarSlot
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
        lines.extend([
            f"{indent}if ((compact_size < {layout.min_size}U) || "
            f"(compact_size > {layout.max_size}U)) {{",
            f"{indent * 2}return -MR_EINVAL;",
            f"{indent}}}",
            f"{indent}p = (mr_uint8_t *){which};",
        ])

        moves: list[tuple[Slot, int, int | None]] = []
        seen_var = False
        var_i = 0
        src_i = 0
        if layout.slots and isinstance(layout.slots[0], FixedSlot):
            lines.append(f"{indent}off = {layout.slots[0].size}U;")
            start_index = 1
        else:
            lines.append(f"{indent}off = 0U;")
            start_index = 0

        for slot in layout.slots[start_index:]:
            n_expr = CEmit.arr("n", var_i, n_count) \
                if isinstance(slot, VarSlot) else None
            src_expr = CEmit.arr("src", src_i, src_count) if seen_var else None
            lines.extend(self._unpack_measure(slot, n_expr, src_expr))
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
            lines.append(f"{indent}if (compact_size != {layout.max_size}U) {{")
            for slot, src_index, n_index in reversed(moves):
                src_expr = CEmit.arr("src", src_index, src_count)
                n_expr = None if n_index is None \
                    else CEmit.arr("n", n_index, n_count)
                lines.extend(self._unpack_move(slot, which, src_expr, n_expr,
                                               indent * 2))
            lines.append(f"{indent}}}")
        # Restore multi-byte native counts after all compact bytes have moved.
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
        moving = set(layout.moving_slots)
        indent = CEmit.INDENT
        lines = [
            f"static mr_err_t {name}(",
            f"{indent}{typedef} *{which}, mr_uint32_t *compact_size) {{",
        ]
        needs_pointer = bool(moving) or any(
            isinstance(slot, VarSlot) and slot.prefix_size > 1
            for slot in layout.slots)
        lines.extend(self._locals(1, 0, has_p=needs_pointer))
        lines.append("")
        if needs_pointer:
            lines.append(f"{indent}p = (mr_uint8_t *){which};")
        if layout.slots and isinstance(layout.slots[0], FixedSlot):
            lines.append(f"{indent}off = {layout.slots[0].size}U;")
            start_index = 1
        else:
            lines.append(f"{indent}off = 0U;")
            start_index = 0

        for slot in layout.slots[start_index:]:
            lines.extend(self._pack_slot(slot, which, slot in moving))
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

    def _unpack_measure(self, slot: Slot, n_expr: str | None,
                        src_expr: str | None) -> list[str]:
        """Validate one compact slot and optionally remember its wire start."""
        indent = CEmit.INDENT
        lines: list[str] = []
        if isinstance(slot, VarSlot):
            prefix = f"{slot.prefix_size}U"
            n_expr = n_expr or "n"
            data_size = self._data_size(slot, n_expr)
            lines.extend([
                f"{indent}if ((compact_size - off) < {prefix}) {{",
                f"{indent * 2}return -MR_EINVAL;",
                f"{indent}}}",
            ])
            lines.extend(self._load_size(slot, n_expr))
            lines.append(f"{indent}off += {prefix};")
            lines.extend([
                f"{indent}if (({n_expr} > {slot.max_count}U) || "
                f"((compact_size - off) < {data_size})) {{",
                f"{indent * 2}return -MR_EINVAL;",
                f"{indent}}}",
            ])
            if src_expr is not None:
                lines.append(f"{indent}{src_expr} = off;")
            lines.append(f"{indent}off += {data_size};")
            return lines
        size = f"{slot.size}U"
        lines.extend([
            f"{indent}if ((compact_size - off) < {size}) {{",
            f"{indent * 2}return -MR_EINVAL;",
            f"{indent}}}",
        ])
        if src_expr is not None:
            lines.append(f"{indent}{src_expr} = off;")
        lines.append(f"{indent}off += {size};")
        return lines

    def _unpack_move(self, slot: Slot, param: str, src_expr: str,
                     n_expr: str | None, indent: str) -> list[str]:
        """Move one compact slot into its scatter field."""
        access = self._access(param, slot)
        inner = indent + CEmit.INDENT
        lines: list[str] = []
        if isinstance(slot, VarSlot):
            n_val = n_expr or "n"
            data_size = self._data_size(slot, n_val)
            data_off = slot.scatter_offset + slot.prefix_size
            lines.append(
                f"{indent}if (({n_val} != 0U) && ({src_expr} != {data_off}U)) {{")
            lines.extend(self._memmove(f"{access}.data", f"p + {src_expr}",
                                       data_size, inner))
            lines.append(f"{indent}}}")
            if slot.prefix_size == 1:
                lines.append(f"{indent}{access}.size = (mr_uint8_t){n_val};")
            return lines
        lines.append(f"{indent}if ({src_expr} != {slot.scatter_offset}U) {{")
        if slot.size == 1:
            lines.append(f"{inner}*(mr_uint8_t *)&{access} = p[{src_expr}];")
        else:
            lines.extend(self._memmove(f"&{access}", f"p + {src_expr}",
                                       f"{slot.size}U", inner))
        lines.append(f"{indent}}}")
        return lines

    def _pack_slot(self, slot: Slot, param: str, move: bool) -> list[str]:
        """Emit scatter-to-compact movement for one slot."""
        access = self._access(param, slot)
        indent = CEmit.INDENT
        lines: list[str] = []
        if isinstance(slot, VarSlot):
            lines.extend([
                f"{indent}n = {access}.size;",
                f"{indent}if (n > {slot.max_count}U) {{",
                f"{indent * 2}return -MR_EINVAL;",
                f"{indent}}}",
            ])
            data_size = self._data_size(slot, "n")
            if move:
                if slot.prefix_size == 1:
                    lines.append(
                        f"{indent}if (off != {slot.scatter_offset}U) {{")
                    lines.append(f"{indent * 2}p[off] = (mr_uint8_t)n;")
                    lines.append(f"{indent * 2}if (n != 0U) {{")
                    lines.extend(self._memmove(
                        "p + off + 1U", f"{access}.data", data_size,
                        indent * 3))
                    lines.append(f"{indent * 2}}}")
                    lines.append(f"{indent}}}")
                else:
                    for byte in range(slot.prefix_size):
                        target = "p[off]" if byte == 0 else f"p[off + {byte}U]"
                        value = "n" if byte == 0 else f"(n >> {byte * 8}U)"
                        lines.append(f"{indent}{target} = (mr_uint8_t){value};")
                    lines.append(
                        f"{indent}if ((n != 0U) && "
                        f"(off != {slot.scatter_offset}U)) {{")
                    lines.extend(self._memmove(
                        f"p + off + {slot.prefix_size}U", f"{access}.data",
                        data_size, indent * 2))
                    lines.append(f"{indent}}}")
            elif slot.prefix_size > 1:
                for byte in range(slot.prefix_size):
                    target = "p[off]" if byte == 0 else f"p[off + {byte}U]"
                    value = "n" if byte == 0 else f"(n >> {byte * 8}U)"
                    lines.append(f"{indent}{target} = (mr_uint8_t){value};")
            lines.append(f"{indent}off += {slot.prefix_size}U + {data_size};")
            return lines
        length = f"{slot.size}U"
        if move:
            lines.append(f"{indent}if (off != {slot.scatter_offset}U) {{")
            if slot.size == 1:
                lines.append(
                    f"{indent * 2}p[off] = *(const mr_uint8_t *)&{access};")
            else:
                lines.extend(self._memmove("p + off", f"&{access}", length,
                                           indent * 2))
            lines.append(f"{indent}}}")
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

    def _memmove(self, dst: str, src: str, size: str,
                 indent: str = CEmit.INDENT) -> list[str]:
        """Format one `mr_memmove`, wrapping when the line is too long."""
        joined = f"{indent}mr_memmove({dst}, {src}, {size});"
        if len(joined) <= CEmit.LINE_WIDTH:
            return [joined]
        return [
            f"{indent}mr_memmove({dst},",
            f"{indent}{CEmit.INDENT}{src}, {size});",
        ]

    @staticmethod
    def _access(param: str, slot: Slot) -> str:
        """Return a `param->field` expression for one slot."""
        return f"{param}->{slot.path}"

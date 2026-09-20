#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Names - C identifier mapping for header and source emission.

Maps IDL names onto generated `__mr_rpc_idl_` tags, request/response
typedefs, RPC call stages, shared empty payload helpers, user entry names,
and codec helpers.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""


class CNames:
    """Builds C identifiers from IDL names."""

    PREFIX = "__mr_rpc_idl_"
    RPC_PREFIX = "__mr_rpc_"
    CODEC_PREFIX = "rpc_"
    MACRO_PREFIX = "MR_RPC_IDL_"
    ENTRY_MACRO_PREFIX = "__MR_RPC_ENTRY_"
    INT32_MIN = -2147483648
    INT64_MIN = -9223372036854775808
    SCALARS = {
        "u8": "mr_uint8_t",
        "u16": "mr_uint16_t",
        "u32": "mr_uint32_t",
        "u64": "mr_uint64_t",
        "i8": "mr_int8_t",
        "i16": "mr_int16_t",
        "i32": "mr_int32_t",
        "i64": "mr_int64_t",
        "bool": "mr_uint8_t",
        "f32": "mr_f32_t",
        "f64": "mr_f64_t",
    }

    @classmethod
    def user_tag(cls, name: str) -> str:
        """Return the struct tag for a user type."""
        return f"{cls.PREFIX}{name}"

    @classmethod
    def user_typedef(cls, name: str) -> str:
        """Return the typedef name for a user type."""
        return f"{cls.PREFIX}{name}_t"

    @classmethod
    def variable_array_tag(cls, index: int) -> str:
        """Return the struct tag for one interned variable-array type."""
        return f"{cls.PREFIX}var_array_{index}"

    @classmethod
    def variable_array_typedef(cls, index: int) -> str:
        """Return the typedef name for one interned variable-array type."""
        return f"{cls.variable_array_tag(index)}_t"

    @classmethod
    def method_request_tag(cls, service: str, method: str) -> str:
        """Return the struct tag for a method request."""
        return f"{cls.RPC_PREFIX}request_{service}_{method}"

    @classmethod
    def method_request_typedef(cls, service: str, method: str) -> str:
        """Return the typedef name for a method request."""
        return f"{cls.method_request_tag(service, method)}_t"

    @classmethod
    def method_response_tag(cls, service: str, method: str) -> str:
        """Return the struct tag for a method response."""
        return f"{cls.RPC_PREFIX}response_{service}_{method}"

    @classmethod
    def method_response_typedef(cls, service: str, method: str) -> str:
        """Return the typedef name for a method response."""
        return f"{cls.method_response_tag(service, method)}_t"

    @classmethod
    def payload_typedef(cls, service: str, method: str, which: str) -> str:
        """Return the request or response typedef for one method."""
        if which == "request":
            return cls.method_request_typedef(service, method)
        return cls.method_response_typedef(service, method)

    @classmethod
    def entry_fn(cls, service: str, method: str) -> str:
        """Return the generated RPC call descriptor name."""
        return f"{service}_{method}"

    @classmethod
    def decode_fn(cls, service: str, method: str) -> str:
        """Return the generated request decode function name."""
        return f"{cls.CODEC_PREFIX}decode_{service}_{method}"

    @classmethod
    def invoke_fn(cls, service: str, method: str) -> str:
        """Return the generated user invoke function name."""
        return f"{cls.CODEC_PREFIX}invoke_{service}_{method}"

    @classmethod
    def encode_fn(cls, service: str, method: str) -> str:
        """Return the generated response encode function name."""
        return f"{cls.CODEC_PREFIX}encode_{service}_{method}"

    @classmethod
    def empty_decode_fn(cls) -> str:
        """Return the shared empty-request decode function name."""
        return f"{cls.CODEC_PREFIX}decode_empty"

    @classmethod
    def empty_encode_fn(cls) -> str:
        """Return the shared empty-response encode function name."""
        return f"{cls.CODEC_PREFIX}encode_empty"

    @classmethod
    def stage_decode_fn(cls, service: str, method: str,
                        has_request: bool) -> str:
        """Return the decode symbol used by one method export."""
        if has_request:
            return cls.decode_fn(service, method)
        return cls.empty_decode_fn()

    @classmethod
    def stage_encode_fn(cls, service: str, method: str, oneway: bool,
                        has_response: bool) -> str:
        """Return the encode symbol used by one method export."""
        if (not oneway) and has_response:
            return cls.encode_fn(service, method)
        return cls.empty_encode_fn()

    @classmethod
    def handler_fn(cls, service: str, method: str) -> str:
        """Return the user handler entry function name."""
        return f"{cls.RPC_PREFIX}entry_{service}_{method}"

    @classmethod
    def export_macro(cls, service: str, method: str) -> str:
        """Return the generated `MR_RPC` expansion macro name."""
        return f"{cls.ENTRY_MACRO_PREFIX}{service}_{method}"

    @classmethod
    def unpack_fn(cls, service: str, method: str, which: str) -> str:
        """Return the generated compact-to-scatter unpack function name."""
        return f"{cls.CODEC_PREFIX}{which}_{service}_{method}_unpack"

    @classmethod
    def pack_fn(cls, service: str, method: str, which: str) -> str:
        """Return the generated scatter-to-compact pack function name."""
        return f"{cls.CODEC_PREFIX}{which}_{service}_{method}_pack"

    @classmethod
    def enum_literal(cls, backing: str, value: int) -> str:
        """Return a C99 integer constant for one enum item value."""
        if backing in ("u8", "u16", "u32"):
            return f"{value}U"
        if backing == "u64":
            return f"{value}ULL"
        if backing == "i64":
            if value == cls.INT64_MIN:
                return "-9223372036854775807LL - 1LL"
            return f"{value}LL"
        if backing == "i32" and value == cls.INT32_MIN:
            return "-2147483647 - 1"
        return str(value)

    @classmethod
    def handler_params(cls, service: str, method: str, has_request: bool,
                       has_response: bool, named: bool = True) -> list[str]:
        """Return C parameter declarations for one handler.

        Named `session` parameters include `MR_UNUSED` so user entries
        do not need `MR_IGNORE(session)` when the session is unused.
        """
        session = "MR_UNUSED mr_rpc_session_t *session" if named \
            else "mr_rpc_session_t *"
        params = [session]
        if has_request:
            typedef = cls.method_request_typedef(service, method)
            params.append(f"{typedef} *request" if named else f"{typedef} *")
        if has_response:
            typedef = cls.method_response_typedef(service, method)
            params.append(f"{typedef} *response" if named else f"{typedef} *")
        return params

    @classmethod
    def enum_const(cls, enum: str, item: str) -> str:
        """Return the public preprocessor constant for one enum item."""
        return f"{cls.MACRO_PREFIX}{enum.upper()}_{item.upper()}"

    @staticmethod
    def union_member(service: str, method: str) -> str:
        """Return the union field name for one method."""
        return f"{service}_{method}"

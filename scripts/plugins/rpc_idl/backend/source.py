#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Source - C source emission for the RPC IDL compiler.

Renders RPC decode/invoke/encode stages, shared empty payload helpers,
and in-place compact-to-scatter unpack and scatter-to-compact pack
helpers.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Callable

from .layout import BoolBound, Layout, VarSlot, bool_index_name, bool_paths
from ..semantic.model import Field, Method, Schema, Service
from .codec import CodecBuilder
from .names import CNames
from .schema import SchemaSourceBuilder
from .emit import CEmit

_BoolErrorHandler = Callable[[str], list[str]]


class SourceBuilder:
    """Renders a schema into generated C RPC call stages."""

    def __init__(self) -> None:
        """Create a source builder with a shared codec emitter."""
        self._codec = CodecBuilder()
        self._schema = SchemaSourceBuilder()

    def build(self, schema: Schema) -> str:
        """Render and wrap one generated source file."""
        return self.wrap(self.render(schema))

    def render(self, schema: Schema) -> str:
        """Render the source body for one schema."""
        lines: list[str] = self._schema.render(schema)
        lines.append("")
        layouts = Layout.map_schema(schema)
        if self._need_empty_decode(schema):
            lines.extend(self._empty_decode_lines())
            lines.append("")
        if self._need_empty_encode(schema):
            lines.extend(self._empty_encode_lines())
            lines.append("")
        for service in schema.services.values():
            for method in service.methods:
                lines.append(CEmit.banner(f"{service.name}/{method.name}"))
                request = layouts.get((service.name, method.name, "request"))
                response = layouts.get((service.name, method.name, "response"))
                request_var = request is not None and request.has_variable
                if request is not None and request.has_variable:
                    lines.extend(self._codec.unpack_lines(
                        service, method, "request", request))
                    lines.append("")
                if response is not None and response.has_variable:
                    lines.extend(self._codec.pack_lines(
                        service, method, "response", response))
                    lines.append("")
                decode = self._decode_lines(
                    service, method, request_var, schema)
                if decode:
                    lines.extend(decode)
                    lines.append("")
                lines.extend(self._invoke_lines(
                    service, method, response))
                lines.append("")
                encode = self._encode_lines(
                    service, method, response, schema)
                if encode:
                    lines.extend(encode)
                    lines.append("")
                lines.extend(self._export_lines(service, method))
                lines.append("")
        return "\n".join(lines).rstrip() + "\n"

    @classmethod
    def wrap(cls, source_text: str) -> str:
        """Wrap rendered entries with the generated source scaffold."""
        lines = [
            CEmit.generated_at(),
            "",
            "#include <mrX/modules/rpc/mr_rpc.h>",
            "#if defined(MR_USE_MODULE_RPC)",
            "#include <mrX/libc/mr_string.h>",
            "",
        ]
        body = source_text.strip()
        if body:
            lines.append(body)
        lines.extend([
            "#endif /* defined(MR_USE_MODULE_RPC) */",
            "",
        ])
        return "\n".join(lines)

    def _decode_lines(self, service: Service, method: Method,
                      request_var: bool, schema: Schema) -> list[str]:
        """Emit one request decode stage."""
        if not method.request:
            return []
        name = CNames.decode_fn(service.name, method.name)
        member = CNames.union_member(service.name, method.name)
        indent = CEmit.INDENT
        typedef = CNames.method_request_typedef(service.name, method.name)
        lines = [f"static mr_err_t {name}(mr_rpc_session_t *session) {{"]
        lines.append(f"{indent}{typedef} *request;")
        if request_var:
            lines.append(f"{indent}mr_err_t ret;")
        lines.append("")
        lines.append(f"{indent}/* Request */")
        lines.append(
            f"{indent}request = &session->request.payload.{member};")
        lines.append("")
        lines.append(f"{indent}/* Unpack */")
        lines.extend(self._decode_request(service, method, request_var))
        bool_lines = self._bool_validate(
            "request", method.request, schema, indent,
            self._decode_bool_error)
        if bool_lines:
            lines.append("")
            lines.extend(bool_lines)
        lines.append(f"{indent}return 0;")
        lines.append("}")
        return lines

    def _invoke_lines(self, service: Service, method: Method,
                      response_layout: Layout | None) -> list[str]:
        """Emit one user invoke stage."""
        name = CNames.invoke_fn(service.name, method.name)
        member = CNames.union_member(service.name, method.name)
        has_request = bool(method.request)
        has_response = (not method.oneway) and bool(method.response)
        indent = CEmit.INDENT
        lines = [f"static mr_err_t {name}(mr_rpc_session_t *session) {{"]

        lines.extend(self._handler_extern_lines(service.name, method))
        if has_response:
            typedef = CNames.method_response_typedef(service.name, method.name)
            lines.append(f"{indent}{typedef} *response;")
        if has_request:
            typedef = CNames.method_request_typedef(service.name, method.name)
            lines.append(f"{indent}{typedef} *request;")
        lines.append("")

        if has_request or has_response:
            comment = self._payload_comment(has_request, has_response)
            lines.append(f"{indent}{comment}")
            if has_request:
                lines.append(
                    f"{indent}request = "
                    f"&session->request.payload.{member};")
            if has_response:
                lines.append(
                    f"{indent}response = "
                    f"&session->response.payload.{member};")
            lines.append("")

        init_lines = self._response_init_lines(response_layout, has_response)
        if init_lines:
            lines.extend(init_lines)
            lines.append("")
        call = self._handler_call(service.name, method, has_request,
                                  has_response)
        lines.append(f"{indent}/* Call user entry */")
        lines.append(f"{indent}return {call}")
        lines.append("}")
        return lines

    def _encode_lines(self, service: Service, method: Method,
                      response_layout: Layout | None,
                      schema: Schema) -> list[str]:
        """Emit one response encode stage."""
        has_response = (not method.oneway) and bool(method.response)
        if not has_response:
            return []
        name = CNames.encode_fn(service.name, method.name)
        member = CNames.union_member(service.name, method.name)
        response_var = (response_layout is not None
                        and response_layout.has_variable)
        indent = CEmit.INDENT
        lines = [f"static mr_err_t {name}(mr_rpc_session_t *session) {{"]

        typedef = CNames.method_response_typedef(service.name, method.name)
        lines.append(f"{indent}{typedef} *response;")
        if response_var:
            lines.append(f"{indent}mr_uint32_t payload_size;")
            lines.append(f"{indent}mr_err_t ret;")
        lines.extend([
            "",
            f"{indent}/* Response */",
            f"{indent}response = &session->response.payload.{member};",
        ])
        bool_lines = self._bool_validate(
            "response", method.response, schema, indent,
            self._decode_bool_error)
        if bool_lines:
            lines.append("")
            lines.extend(bool_lines)
        empty_lines = self._stream_empty_lines(response_layout)
        if empty_lines:
            lines.append("")
            lines.extend(empty_lines)
        if response_var:
            pack = CNames.pack_fn(service.name, method.name, "response")
            lines.extend([
                "",
                f"{indent}/* Pack */",
                f"{indent}ret = {pack}(response, &payload_size);",
                f"{indent}if (ret < 0) {{",
                f"{indent * 2}return ret;",
                f"{indent}}}",
                f"{indent}return (mr_err_t)payload_size;",
            ])
        else:
            lines.append(f"{indent}return (mr_err_t)sizeof(*response);")
        lines.append("}")
        return lines

    @staticmethod
    def _export_lines(service: Service, method: Method) -> list[str]:
        """Emit one RPC call descriptor export."""
        entry = CNames.entry_fn(service.name, method.name)
        decode = CNames.stage_decode_fn(service.name, method.name,
                                        bool(method.request))
        invoke = CNames.invoke_fn(service.name, method.name)
        encode = CNames.stage_encode_fn(service.name, method.name,
                                        method.oneway, bool(method.response))
        return [
            f"MR_RPC_EXPORT({entry},",
            f"              {decode},",
            f"              {invoke},",
            f"              {encode},",
            f"              {service.service_id},",
            f"              {method.method_id});",
        ]

    @staticmethod
    def _need_empty_decode(schema: Schema) -> bool:
        """Return whether any method uses the shared empty decode."""
        return any(not method.request for service in schema.services.values()
                   for method in service.methods)

    @staticmethod
    def _need_empty_encode(schema: Schema) -> bool:
        """Return whether any method uses the shared empty encode."""
        return any(method.oneway or (not method.response)
                   for service in schema.services.values()
                   for method in service.methods)

    @staticmethod
    def _empty_decode_lines() -> list[str]:
        """Emit the shared empty-request decode helper."""
        name = CNames.empty_decode_fn()
        indent = CEmit.INDENT
        return [
            f"static mr_err_t {name}(mr_rpc_session_t *session) {{",
            f"{indent}if (session->recv.size != 0U) {{",
            f"{indent * 2}return -MR_EINVAL;",
            f"{indent}}}",
            f"{indent}return 0;",
            "}",
        ]

    @staticmethod
    def _empty_encode_lines() -> list[str]:
        """Emit the shared empty-response encode helper."""
        name = CNames.empty_encode_fn()
        indent = CEmit.INDENT
        return [
            f"static mr_err_t {name}(mr_rpc_session_t *session) {{",
            f"{indent}MR_IGNORE(session);",
            f"{indent}return 0;",
            "}",
        ]

    def _bool_validate(self, param: str, fields: list[Field], schema: Schema,
                       indent: str,
                       on_error: _BoolErrorHandler) -> list[str]:
        """Emit recursive bool 0/1 checks for request or response fields."""
        paths = bool_paths(fields, schema)
        if not paths:
            return []
        lines = [f"{indent}/* Validate */"]
        for access, dims in paths:
            lines.extend(self._bool_check(param, access, dims, indent,
                                          on_error))
        return lines

    def _bool_check(self, param: str, access: str,
                    dims: tuple[BoolBound, ...], indent: str,
                    on_error: _BoolErrorHandler) -> list[str]:
        """Emit one bool field check, with fixed or runtime array loops."""
        cond = f"{param}->{access} > 1U"
        if not dims:
            return self._bool_if(f"{indent}if ({cond}) {{", indent, on_error)
        names = [bool_index_name(depth) for depth in range(len(dims))]
        lines = [
            f"{indent}{{",
            f"{indent}{CEmit.INDENT}mr_uint32_t {', '.join(names)};",
        ]
        for depth, bound in enumerate(dims):
            name = names[depth]
            pad = indent + CEmit.INDENT * (depth + 1)
            if isinstance(bound, int):
                condition = f"{name} < {bound}U"
            else:
                size_path, max_count = bound
                condition = (
                    f"({name} < {param}->{size_path}.size) && "
                    f"({name} < {max_count}U)"
                )
            lines.append(
                f"{pad}for ({name} = 0U; {condition}; {name}++) {{")
        pad = indent + CEmit.INDENT * (len(dims) + 1)
        lines.extend(self._bool_if(f"{pad}if ({cond}) {{", pad, on_error))
        for depth in range(len(dims) - 1, -1, -1):
            pad = indent + CEmit.INDENT * (depth + 1)
            lines.append(f"{pad}}}")
        lines.append(f"{indent}}}")
        return lines

    @staticmethod
    def _bool_if(header: str, indent: str,
                 on_error: _BoolErrorHandler) -> list[str]:
        """Emit one `if (bool > 1)` body using the shared error callback."""
        lines = [header]
        lines.extend(on_error(indent + CEmit.INDENT))
        lines.append(f"{indent}}}")
        return lines

    @staticmethod
    def _decode_bool_error(indent: str) -> list[str]:
        """Return `-MR_EINVAL` from a decode or encode stage."""
        return [f"{indent}return -MR_EINVAL;"]

    @staticmethod
    def _stream_empty_lines(response_layout: Layout | None) -> list[str]:
        """Skip wire-only length prefixes for an empty stream response."""
        if response_layout is None or not response_layout.variable_only:
            return []
        indent = CEmit.INDENT
        slots = [slot for slot in response_layout.slots
                 if isinstance(slot, VarSlot)]
        lines = [
            f"{indent}/* Skip an empty stream response */",
            f"{indent}if (session->request.type == MR_RPC_MSG_STREAM) {{",
        ]
        for index, slot in enumerate(slots):
            prefix = "if" if index == 0 else "&&"
            pad = indent * 2 if index == 0 else indent * 3
            if index == 0:
                lines.append(
                    f"{pad}{prefix} ((response->{slot.path}.size == 0U)")
            else:
                lines.append(
                    f"{pad}{prefix} (response->{slot.path}.size == 0U)")
        lines[-1] += ") {"
        lines.extend([
            f"{indent * 3}return 0;",
            f"{indent * 2}}}",
            f"{indent}}}",
        ])
        return lines

    @staticmethod
    def _response_init_lines(response_layout: Layout | None,
                             has_response: bool) -> list[str]:
        """Zero each response variable-length size before the handler."""
        if not has_response or response_layout is None:
            return []
        slots = [slot for slot in response_layout.slots
                 if isinstance(slot, VarSlot)]
        if not slots:
            return []
        indent = CEmit.INDENT
        lines = [f"{indent}/* Init response */"]
        for slot in slots:
            lines.append(f"{indent}response->{slot.path}.size = 0U;")
        return lines

    def _decode_request(self, service: Service, method: Method,
                        request_var: bool) -> list[str]:
        """Emit request unpack or a fixed-size payload check."""
        indent = CEmit.INDENT
        if request_var:
            unpack = CNames.unpack_fn(service.name, method.name, "request")
            call = f"{unpack}(request, session->recv.size)"
            joined = f"{indent}ret = {call};"
            if len(joined) <= CEmit.LINE_WIDTH:
                return [
                    joined,
                    f"{indent}if (ret < 0) {{",
                    f"{indent * 2}return ret;",
                    f"{indent}}}",
                ]
            return [
                f"{indent}ret = {unpack}(",
                f"{indent * 2}request, session->recv.size);",
                f"{indent}if (ret < 0) {{",
                f"{indent * 2}return ret;",
                f"{indent}}}",
            ]
        expected = "sizeof(*request)"
        return [
            f"{indent}if (session->recv.size != {expected}) {{",
            f"{indent * 2}return -MR_EINVAL;",
            f"{indent}}}",
        ]

    def _handler_extern_lines(self, service: str, method: Method) -> list[str]:
        """Format the `extern` prototype of the user handler."""
        decl = CEmit.handler_decl(service, method, indent="    extern ",
                                  named=False)
        decl[-1] = f"{decl[-1]};"
        return decl

    def _handler_call(self, service: str, method: Method, has_request: bool,
                      has_response: bool) -> str:
        """Format the user handler call, including the trailing semicolon."""
        name = CNames.handler_fn(service, method.name)
        args = ["session"]
        if has_request:
            args.append("request")
        if has_response:
            args.append("response")
        return f"{name}({', '.join(args)});"

    @staticmethod
    def _payload_comment(has_request: bool, has_response: bool) -> str:
        """Return the short comment above payload pointer assignment."""
        if has_request and has_response:
            return "/* Request & response */"
        if has_request:
            return "/* Request */"
        return "/* Response */"

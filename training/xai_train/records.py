"""Strict JSONL record validation for the XAI text-perception training pipeline."""
from __future__ import annotations

from typing import Any

ALLOWED_ORIGINS = {"natural", "human_translation"}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def _is_utf8_boundary(raw: bytes, offset: int) -> bool:
    if not 0 <= offset <= len(raw):
        return False
    try:
        raw[:offset].decode("utf-8", errors="strict")
        raw[offset:].decode("utf-8", errors="strict")
        return True
    except UnicodeDecodeError:
        return False


def validate_graph(graph: Any, text: str) -> dict[str, Any]:
    _require(isinstance(graph, dict), "graph must be an object")
    nodes = graph.get("nodes")
    edges = graph.get("edges")
    _require(isinstance(nodes, list) and isinstance(edges, list), "graph nodes and edges must be arrays")
    raw = text.encode("utf-8", errors="strict")
    ids: set[str] = set()
    for node in nodes:
        _require(isinstance(node, dict), "node must be an object")
        node_id = node.get("id")
        _require(isinstance(node_id, str) and node_id and node_id not in ids, "node ids must be unique non-empty strings")
        ids.add(node_id)
        _require(isinstance(node.get("type"), str) and node["type"], "node type is required")
        _require(isinstance(node.get("label"), str) and node["label"], "node label is required")
        spans = node.get("source_spans", [])
        _require(isinstance(spans, list), "node source_spans must be an array")
        implicit = node.get("implicit", False)
        _require(isinstance(implicit, bool), "node implicit must be boolean")
        _require(bool(spans) or implicit, "a node must have an exact source span or be explicitly implicit")
        for span in spans:
            _require(isinstance(span, dict), "source span must be an object")
            begin, end = span.get("begin"), span.get("end")
            _require(type(begin) is int and type(end) is int and 0 <= begin < end <= len(raw), "source span must be a non-empty in-range UTF-8 byte interval")
            _require(_is_utf8_boundary(raw, begin) and _is_utf8_boundary(raw, end), "source span splits a UTF-8 scalar")
        attributes = node.get("attributes", {})
        _require(isinstance(attributes, dict), "node attributes must be an object")
        for key, value in attributes.items():
            _require(isinstance(key, str) and key and isinstance(value, (str, bool, int)), "attributes must be named scalar values")
    for edge in edges:
        _require(isinstance(edge, dict), "edge must be an object")
        _require(edge.get("source") in ids and edge.get("target") in ids, "edge endpoints must reference nodes in this graph")
        _require(isinstance(edge.get("relation"), str) and edge["relation"], "edge relation is required")
    if "speech_act" in graph:
        _require(isinstance(graph["speech_act"], str) and graph["speech_act"], "speech_act must be a non-empty string")
    return graph


def validate_record(record: Any) -> dict[str, Any]:
    _require(isinstance(record, dict), "record must be a JSON object")
    for name in ("record_id", "source_id", "language", "text", "text_origin", "license_id", "rights_status"):
        _require(isinstance(record.get(name), str) and record[name], f"{name} is required")
    _require(record["text_origin"] in ALLOWED_ORIGINS, "synthetic, templated, or unknown text origins are not allowed")
    record["text"].encode("utf-8", errors="strict")
    for name in ("use_pretraining", "use_graph_training"):
        _require(type(record.get(name)) is bool, f"{name} must be boolean")
    readings = record.get("accepted_readings")
    _require(isinstance(readings, list), "accepted_readings must be an array")
    if record["use_graph_training"]:
        _require(bool(readings), "graph-training records need at least one human-accepted reading")
    else:
        _require(not readings, "records without graph-training permission must not carry graph targets")
    for graph in readings:
        validate_graph(graph, record["text"])
    if "duplicate_group" in record:
        _require(isinstance(record["duplicate_group"], str) and record["duplicate_group"], "duplicate_group must be a non-empty string")
    return record


def require_training_rights(record: dict[str, Any]) -> None:
    _require(record["rights_status"] == "approved", f"training blocked: rights are not approved for {record['record_id']} ({record['license_id']})")

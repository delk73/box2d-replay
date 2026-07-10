#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import csv
import math
import re
import sys


REQUIRED_METADATA = {
    "pose_columns": "frame,x,y,angle,vx,vy,angular_velocity",
    "contact_begin_columns": "event,frame,shape_a,shape_b",
    "contact_end_columns": "event,frame,shape_a,shape_b",
    "contact_hit_columns": "event,frame,shape_a,shape_b,normal_x,normal_y,point_x,point_y,approach_speed",
}

DIGEST_RE = re.compile(r"^digest=fnv1a64:[0-9a-f]{16}$")
CONTACT_FIELDS = {
    "contact_begin": 4,
    "contact_end": 4,
    "contact_hit": 9,
}


class WitnessError(Exception):
    pass


def parse_positive_float(name, value):
    try:
        parsed = float(value)
    except ValueError as exc:
        raise WitnessError(f"{name} must parse as a float") from exc

    if not math.isfinite(parsed) or parsed <= 0.0:
        raise WitnessError(f"{name} must be a positive finite float")

    return parsed


def parse_positive_int(name, value):
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise WitnessError(f"{name} must parse as an int") from exc

    if parsed <= 0:
        raise WitnessError(f"{name} must be a positive int")

    return parsed


def parse_float_field(name, value, line_number):
    try:
        parsed = float(value)
    except ValueError as exc:
        raise WitnessError(f"line {line_number}: {name} must parse as a float") from exc

    if not math.isfinite(parsed):
        raise WitnessError(f"line {line_number}: {name} must be finite")

    return parsed


def parse_int_field(name, value, line_number):
    try:
        return int(value, 10)
    except ValueError as exc:
        raise WitnessError(f"line {line_number}: {name} must parse as an int") from exc


def parse_csv_line(line, line_number):
    try:
        fields = next(csv.reader([line]))
    except csv.Error as exc:
        raise WitnessError(f"line {line_number}: invalid CSV row") from exc

    if any(field == "" for field in fields):
        raise WitnessError(f"line {line_number}: empty CSV field")

    return fields


def validate_pose_row(fields, expected_frame, line_number):
    if len(fields) != 7:
        raise WitnessError(f"line {line_number}: pose row must have 7 fields")

    frame = parse_int_field("frame", fields[0], line_number)
    if frame != expected_frame:
        raise WitnessError(f"line {line_number}: pose frame must be {expected_frame}")

    parse_float_field("x", fields[1], line_number)
    parse_float_field("y", fields[2], line_number)
    parse_float_field("angle", fields[3], line_number)
    parse_float_field("vx", fields[4], line_number)
    parse_float_field("vy", fields[5], line_number)
    parse_float_field("angular_velocity", fields[6], line_number)


def validate_contact_row(fields, frames_value, line_number):
    event = fields[0]
    expected_count = CONTACT_FIELDS.get(event)
    if expected_count is None:
        raise WitnessError(f"line {line_number}: unknown contact row type")

    if len(fields) != expected_count:
        raise WitnessError(f"line {line_number}: {event} row must have {expected_count} fields")

    frames = parse_positive_int("frames", frames_value)
    frame = parse_int_field("contact frame", fields[1], line_number)
    if frame < 0 or frame >= frames:
        raise WitnessError(f"line {line_number}: contact frame must be within [0, frames)")

    if event == "contact_hit":
        parse_float_field("normal_x", fields[4], line_number)
        parse_float_field("normal_y", fields[5], line_number)
        parse_float_field("point_x", fields[6], line_number)
        parse_float_field("point_y", fields[7], line_number)
        parse_float_field("approach_speed", fields[8], line_number)


def validate_witness(lines):
    if not lines:
        raise WitnessError("witness file is empty")

    if not DIGEST_RE.match(lines[-1]):
        raise WitnessError("final line must be digest=fnv1a64:<16 lowercase hex>")

    metadata = {}
    pose_count = 0

    for line_number, line in enumerate(lines[:-1], start=1):
        if "=" in line:
            key, value = line.split("=", 1)
            if key in REQUIRED_METADATA or not key.startswith("contact_"):
                metadata[key] = value
                continue

        fields = parse_csv_line(line, line_number)
        if fields[0].startswith("contact_"):
            if "frames" not in metadata:
                raise WitnessError(f"line {line_number}: contact row appears before frames metadata")
            validate_contact_row(fields, metadata["frames"], line_number)
        else:
            validate_pose_row(fields, pose_count, line_number)
            pose_count += 1

    if "scenario" not in metadata:
        raise WitnessError("missing scenario metadata")
    if metadata["scenario"] == "":
        raise WitnessError("scenario metadata must not be empty")

    if "dt" not in metadata:
        raise WitnessError("missing dt metadata")
    parse_positive_float("dt", metadata["dt"])

    if "substeps" not in metadata:
        raise WitnessError("missing substeps metadata")
    parse_positive_int("substeps", metadata["substeps"])

    if "frames" not in metadata:
        raise WitnessError("missing frames metadata")
    frames = parse_positive_int("frames", metadata["frames"])
    metadata["frames"] = frames

    for key, expected_value in REQUIRED_METADATA.items():
        if key not in metadata:
            raise WitnessError(f"missing {key} metadata")
        if metadata[key] != expected_value:
            raise WitnessError(f"{key} metadata does not match expected schema")

    if pose_count != frames:
        raise WitnessError(f"pose row count {pose_count} does not match frames {frames}")


def main(argv):
    if len(argv) != 2:
        print(f"usage: {argv[0]} WITNESS_FILE", file=sys.stderr)
        return 2

    try:
        with open(argv[1], "r", encoding="utf-8", newline="") as witness_file:
            lines = [line.rstrip("\n") for line in witness_file]
        validate_witness(lines)
    except OSError as exc:
        print(f"check_witness.py: {exc}", file=sys.stderr)
        return 2
    except WitnessError as exc:
        print(f"check_witness.py: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

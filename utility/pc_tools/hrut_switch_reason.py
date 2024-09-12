#!/usr/bin/env python3
#
# Horizon switch reason PC tool
# Maintainer: zhiwei01.li
# Copyright (C) 2024 D-Robotics
#

import sys
import struct
import zlib

MAX_SWITCH_REASON_RECORD = 10  # Define the maximum number of switch reasons
SWITCH_REASON_OFFSET = 3 * 1024
SWITCH_REASON_S = [
    "unused",
    "misc broken",
    "boot corruted",
    "dm-verity corruped",
    "uboot corruped",
    "miniboot corruped",
    "misc info err",
]
MAX_REASON_COUNT = len(SWITCH_REASON_S)
# Define the switch_reason structure format
switch_reason_format = f'I{MAX_SWITCH_REASON_RECORD}II'


def calculate_crc32(data):
    """Calculate CRC32 for the given data."""
    return zlib.crc32(data) & 0xFFFFFFFF


def read_switch_reason(filename):
    """Read switch_reason structure from a binary file starting"""
    """at offset 3072 bytes."""
    try:
        with open(filename, 'rb') as file:
            # Move to the 3072nd byte in the file
            file.seek(SWITCH_REASON_OFFSET)
            # Read data for switch_reason structure
            data = file.read(struct.calcsize(switch_reason_format))
            if len(data) != struct.calcsize(switch_reason_format):
                raise ValueError(
                    "Failed to read sufficient data "
                    "for switch_reason structure.")

            # Unpack the data according to the defined format
            unpacked_data = struct.unpack(switch_reason_format, data)
            current = unpacked_data[0]
            reasons = unpacked_data[1:1 + MAX_SWITCH_REASON_RECORD]
            crc32_le = unpacked_data[-1]

            # Return the unpacked data
            # Return data excluding crc32_le for CRC check
            return current, reasons, crc32_le, data[:-4]
    except FileNotFoundError:
        print(f"Failed to open file: {filename}")
        sys.exit(1)
    except Exception as e:
        print(f"Error reading data: {e}")
        sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <misc_file> <--current|--all>")
        sys.exit(1)

    if len(sys.argv) == 2 and (sys.argv[1] == "-h" or sys.argv[1] == "--help"):
        print(f"Usage: {sys.argv[0]} <misc_file> <--current|--all>")
        sys.exit(0)

    filename = sys.argv[1]
    option = sys.argv[2]

    # Read the switch_reason structure from the file
    current, reasons, crc32_le, crc_data = read_switch_reason(filename)

    # Perform CRC32 check
    calculated_crc32 = calculate_crc32(crc_data)
    if calculated_crc32 != crc32_le:
        print(
            f"CRC32 check failed. Expected: {hex(crc32_le)}, ",
            f"Calculated: {hex(calculated_crc32)}")
        sys.exit(1)

    # Check the option and print the appropriate data
    if option == "--current":
        if current >= MAX_SWITCH_REASON_RECORD:
            print("Error: Current index is out of range.")
            sys.exit(1)
        if reasons[current] > MAX_REASON_COUNT - 1:
            print(
                f"Error: Invalid reason {reasons[current]}, "
                f"number {current}.")
            sys.exit(1)
        print(f"{SWITCH_REASON_S[reasons[current]]}")
    elif option == "--all":
        print("All Reasons (From the latest to the oldest):")
        for i, reason in enumerate(reasons):
            if reason > MAX_REASON_COUNT - 1:
                print(
                    f"Error: Invalid reason {reasons[current]}, "
                    f"number {i}.")
                sys.exit(1)
            idx = (MAX_SWITCH_REASON_RECORD + current - i) % \
                MAX_SWITCH_REASON_RECORD
            print(f"  {i + 1}: {SWITCH_REASON_S[reasons[idx]]}")
    else:
        print(f"Invalid option: {option}. Please use --current or --all.")
        sys.exit(1)


if __name__ == "__main__":
    main()

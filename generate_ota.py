#!/usr/bin/env python3
"""
Generate a Zigbee OTA upgrade image (.ota) from an ESP-IDF firmware binary.
Compatible with Zigbee2MQTT OTA update mechanism.

Usage:
    python generate_ota.py --version 6
    python generate_ota.py --version 6 --z2m-index /path/to/z2m/data/ota/index.json
"""
import struct
import os
import json
import argparse

OTA_MAGIC = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_STACK_VERSION = 0x0002
OTA_SUBELEMENT_UPGRADE_IMAGE = 0x0000

# Must match config.h
DEFAULT_MANUFACTURER = 0x131B  # Espressif
DEFAULT_IMAGE_TYPE   = 0x1011  # CO2 sensor


def create_ota_image(bin_path, out_path, manufacturer, image_type, version):
    with open(bin_path, 'rb') as f:
        firmware = f.read()

    header_string = b"ESP32C6 CO2 Sensor".ljust(32, b'\x00')
    header_length = 56  # fixed when no optional fields

    subelement = struct.pack('<HI', OTA_SUBELEMENT_UPGRADE_IMAGE, len(firmware)) + firmware
    total_size = header_length + len(subelement)

    header  = struct.pack('<I', OTA_MAGIC)
    header += struct.pack('<H', OTA_HEADER_VERSION)
    header += struct.pack('<H', header_length)
    header += struct.pack('<H', 0x0000)           # field control (no optional fields)
    header += struct.pack('<H', manufacturer)
    header += struct.pack('<H', image_type)
    header += struct.pack('<I', version)
    header += struct.pack('<H', OTA_STACK_VERSION)
    header += header_string                        # 32 bytes
    header += struct.pack('<I', total_size)

    assert len(header) == header_length

    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(subelement)

    return total_size


def main():
    parser = argparse.ArgumentParser(description='Generate Zigbee OTA image for Zigbee2MQTT')
    parser.add_argument('--bin',          default='build/zigbee_co2_sensor.bin',
                        help='Input firmware .bin (default: build/zigbee_co2_sensor.bin)')
    parser.add_argument('--out',          default='zigbee_co2_sensor.ota',
                        help='Output .ota file (default: zigbee_co2_sensor.ota)')
    parser.add_argument('--manufacturer', type=lambda x: int(x, 0), default=DEFAULT_MANUFACTURER,
                        help=f'Manufacturer code (default: 0x{DEFAULT_MANUFACTURER:04X})')
    parser.add_argument('--image-type',  type=lambda x: int(x, 0), default=DEFAULT_IMAGE_TYPE,
                        help=f'Image type (default: 0x{DEFAULT_IMAGE_TYPE:04X})')
    parser.add_argument('--version',     type=lambda x: int(x, 0), required=True,
                        help='File version, must be > current firmware version (e.g. 6 or 0x6)')
    parser.add_argument('--z2m-index',
                        help='Path to Z2M OTA index.json to update (optional)')
    args = parser.parse_args()

    if not os.path.exists(args.bin):
        print(f"ERROR: firmware binary not found: {args.bin}")
        print("Run 'idf.py build' first.")
        raise SystemExit(1)

    total_size = create_ota_image(args.bin, args.out, args.manufacturer, args.image_type, args.version)

    print(f"OTA image created: {args.out}")
    print(f"  Manufacturer : 0x{args.manufacturer:04X}")
    print(f"  Image type   : 0x{args.image_type:04X}")
    print(f"  Version      : 0x{args.version:08X}  ({args.version})")
    print(f"  Total size   : {total_size} bytes")

    entry = {
        "fileName": os.path.abspath(args.out),
        "fileVersion": args.version,
        "fileSize": total_size,
        "manufacturerCode": args.manufacturer,
        "imageType": args.image_type,
    }

    if args.z2m_index:
        if os.path.exists(args.z2m_index):
            with open(args.z2m_index) as f:
                index = json.load(f)
        else:
            os.makedirs(os.path.dirname(args.z2m_index), exist_ok=True)
            index = []

        # Replace any existing entry for this manufacturer + image type
        index = [e for e in index
                 if not (e.get('manufacturerCode') == args.manufacturer
                         and e.get('imageType') == args.image_type)]
        index.append(entry)

        with open(args.z2m_index, 'w') as f:
            json.dump(index, f, indent=2)
        print(f"\nZ2M index updated: {args.z2m_index}")
    else:
        print("\nZ2M index entry (add to your index.json):")
        print(json.dumps(entry, indent=2))


if __name__ == '__main__':
    main()

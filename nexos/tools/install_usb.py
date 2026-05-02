#!/usr/bin/env python3
# NexOS — tools/install_usb.py | USB installer script | MIT License

import subprocess
import os
import sys

def get_removable_drives_linux():
    """Detect removable block devices on Linux."""
    drives = []
    try:
        result = subprocess.run(['lsblk', '-o', 'NAME,SIZE,LABEL,RM,TYPE', '--noheadings'],
                                capture_output=True, text=True)
        for line in result.stdout.strip().split('\n'):
            parts = line.split()
            if len(parts) >= 4 and parts[3] == '1' and parts[4] == 'disk':
                name = parts[0].lstrip('─├└')
                size = parts[1]
                label = parts[2] if len(parts) > 4 else ''
                drives.append({'path': f'/dev/{name}', 'size': size, 'label': label})
    except Exception:
        pass
    return drives

def get_removable_drives_macos():
    """Detect removable drives on macOS."""
    drives = []
    try:
        result = subprocess.run(['diskutil', 'list', '-plist', 'external'],
                                capture_output=True, text=True)
        import plistlib
        data = plistlib.loads(result.stdout.encode())
        for disk in data.get('AllDisksAndPartitions', []):
            dev = disk.get('DeviceIdentifier', '')
            size_bytes = disk.get('Size', 0)
            size_gb = size_bytes / (1024**3)
            drives.append({
                'path': f'/dev/{dev}',
                'size': f'{size_gb:.1f}G',
                'label': disk.get('VolumeName', '')
            })
    except Exception:
        pass
    return drives

def write_iso(iso_path, target_drive):
    """Write ISO to USB drive using dd."""
    cmd = ['dd', f'if={iso_path}', f'of={target_drive}', 'bs=4M', 'status=progress']
    print(f'\n[installer] Writing {iso_path} to {target_drive}...')
    print('[installer] This may take a few minutes.\n')
    try:
        subprocess.run(cmd, check=True)
        subprocess.run(['sync'], check=True)
        print(f'\n[installer] Done! USB drive ready. Boot from it in your BIOS.')
    except subprocess.CalledProcessError as e:
        print(f'[installer] Error: dd failed: {e}')
        sys.exit(1)

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    iso_path = os.path.join(script_dir, '..', 'build', 'nexos.iso')
    iso_path = os.path.normpath(iso_path)

    print('=' * 60)
    print('  NexOS USB Installer')
    print('=' * 60)

    if not os.path.exists(iso_path):
        print(f'[installer] Error: ISO not found at {iso_path}')
        print('[installer] Run "make iso" first to build the ISO.')
        sys.exit(1)

    iso_size = os.path.getsize(iso_path)
    print(f'[installer] ISO: {iso_path} ({iso_size // (1024*1024)} MB)')
    print()

    # Detect OS
    if sys.platform == 'linux':
        drives = get_removable_drives_linux()
    elif sys.platform == 'darwin':
        drives = get_removable_drives_macos()
    else:
        print('[installer] Unsupported OS. Only Linux and macOS are supported.')
        sys.exit(1)

    if not drives:
        print('[installer] No removable USB drives detected.')
        print('[installer] Please insert a USB drive and try again.')
        sys.exit(1)

    print('[installer] Detected USB drives:')
    for i, drive in enumerate(drives):
        label = f' ({drive["label"]})' if drive['label'] else ''
        print(f'  [{i + 1}] {drive["path"]}  {drive["size"]}{label}')

    print()
    choice = input('[installer] Select drive number (or q to quit): ').strip()
    if choice.lower() == 'q':
        print('[installer] Aborted.')
        sys.exit(0)

    try:
        idx = int(choice) - 1
        if idx < 0 or idx >= len(drives):
            raise ValueError
    except ValueError:
        print('[installer] Invalid selection.')
        sys.exit(1)

    target = drives[idx]['path']

    print()
    print(f'!!! WARNING !!!  All data on {target} will be PERMANENTLY ERASED.')
    print()
    confirm = input('Type YES (uppercase) to confirm and proceed: ').strip()
    if confirm != 'YES':
        print('[installer] Aborted. Drive was NOT written.')
        sys.exit(0)

    write_iso(iso_path, target)

if __name__ == '__main__':
    main()

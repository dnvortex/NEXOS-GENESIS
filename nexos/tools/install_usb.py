#!/usr/bin/env python3
# NexOS — tools/install_usb.py | USB installer script | MIT License

import subprocess
import os
import sys
import platform

def get_removable_drives_linux():
    """Detect removable block devices on Linux."""
    drives = []
    try:
        result = subprocess.run(['lsblk', '-o', 'NAME,SIZE,LABEL,RM,TYPE', '--noheadings'],
                                capture_output=True, text=True)
        for line in result.stdout.strip().split('\n'):
            parts = line.split()
            if len(parts) >= 5 and parts[3] == '1' and parts[4] == 'disk':
                name  = parts[0].lstrip('─├└')
                size  = parts[1]
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
            dev       = disk.get('DeviceIdentifier', '')
            size_bytes = disk.get('Size', 0)
            size_gb   = size_bytes / (1024 ** 3)
            drives.append({
                'path':  f'/dev/{dev}',
                'size':  f'{size_gb:.1f}G',
                'label': disk.get('VolumeName', ''),
            })
    except Exception:
        pass
    return drives

def get_removable_drives_windows():
    """Detect removable USB drives on Windows using PowerShell."""
    drives = []
    try:
        ps_cmd = (
            'Get-Disk | Where-Object { $_.BusType -eq "USB" } | '
            'ForEach-Object { '
            '$d = $_; '
            '$parts = Get-Partition -DiskNumber $d.DiskNumber -ErrorAction SilentlyContinue; '
            '$letter = ($parts | Where-Object { $_.DriveLetter } | '
            '           Select-Object -First 1 -ExpandProperty DriveLetter); '
            'Write-Output ("$($d.DiskNumber)|$($d.Size)|$($d.FriendlyName)|$letter") '
            '}'
        )
        result = subprocess.run(
            ['powershell', '-NoProfile', '-Command', ps_cmd],
            capture_output=True, text=True
        )
        for line in result.stdout.strip().split('\n'):
            line = line.strip()
            if not line:
                continue
            parts = line.split('|')
            if len(parts) < 2:
                continue
            disk_num   = parts[0].strip()
            size_bytes = int(parts[1].strip()) if parts[1].strip().isdigit() else 0
            friendly   = parts[2].strip() if len(parts) > 2 else ''
            letter     = parts[3].strip() if len(parts) > 3 else ''
            size_gb    = size_bytes / (1024 ** 3)
            label      = f'{friendly} ({letter}:)' if letter else friendly
            drives.append({
                'path':  f'\\\\.\\PhysicalDrive{disk_num}',
                'size':  f'{size_gb:.1f}G',
                'label': label,
            })
    except Exception as e:
        print(f'[installer] Warning: PowerShell drive detection failed: {e}')
    return drives

def write_iso_unix(iso_path, target_drive):
    """Write ISO to USB drive using dd (Linux/macOS)."""
    cmd = ['dd', f'if={iso_path}', f'of={target_drive}', 'bs=4M', 'status=progress']
    if sys.platform == 'darwin':
        # macOS dd does not support status=progress; use rdisk for speed
        rdisk = target_drive.replace('/dev/disk', '/dev/rdisk')
        cmd = ['dd', f'if={iso_path}', f'of={rdisk}', 'bs=4m']
    print(f'\n[installer] Writing {iso_path} to {target_drive}...')
    print('[installer] This may take a few minutes.\n')
    try:
        subprocess.run(cmd, check=True)
        subprocess.run(['sync'], check=True)
        print(f'\n[installer] Done! USB drive is ready. Boot from it in your BIOS/UEFI.')
    except subprocess.CalledProcessError as e:
        print(f'[installer] Error: write failed: {e}')
        sys.exit(1)

def write_iso_windows(iso_path, target_drive):
    """Write ISO to Windows physical drive using PowerShell + dd (if available) or copy."""
    print(f'\n[installer] Writing {iso_path} to {target_drive}...')
    print('[installer] This may take a few minutes.\n')

    # Try dd for Windows (from https://www.chrysocome.net/dd or Git for Windows)
    dd_candidates = [
        'dd',
        r'C:\Program Files\Git\usr\bin\dd.exe',
        r'C:\tools\dd.exe',
    ]
    dd_exe = None
    for candidate in dd_candidates:
        try:
            subprocess.run([candidate, '--version'], capture_output=True, check=True)
            dd_exe = candidate
            break
        except Exception:
            pass

    if dd_exe:
        cmd = [dd_exe, f'if={iso_path}', f'of={target_drive}', 'bs=4M']
        try:
            subprocess.run(cmd, check=True)
            print('\n[installer] Done! USB drive is ready.')
            return
        except subprocess.CalledProcessError as e:
            print(f'[installer] dd failed: {e}')

    # Fallback: PowerShell Write-Disk
    ps_cmd = (
        f'$iso = [System.IO.File]::OpenRead("{iso_path}"); '
        f'$disk = [System.IO.File]::OpenWrite("{target_drive}"); '
        f'$buf = New-Object byte[] 4MB; '
        f'while (($n = $iso.Read($buf,0,$buf.Length)) -gt 0) {{ $disk.Write($buf,0,$n) }}; '
        f'$iso.Close(); $disk.Flush(); $disk.Close()'
    )
    try:
        subprocess.run(['powershell', '-NoProfile', '-Command', ps_cmd], check=True)
        print('\n[installer] Done! USB drive is ready.')
    except subprocess.CalledProcessError as e:
        print(f'[installer] Error: write failed: {e}')
        sys.exit(1)

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    iso_path   = os.path.normpath(os.path.join(script_dir, '..', 'build', 'nexos.iso'))

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

    # Detect OS and enumerate drives
    os_name = platform.system()
    if os_name == 'Linux':
        drives = get_removable_drives_linux()
    elif os_name == 'Darwin':
        drives = get_removable_drives_macos()
    elif os_name == 'Windows':
        drives = get_removable_drives_windows()
    else:
        print(f'[installer] Unsupported OS: {os_name}')
        print('[installer] Supported: Linux, macOS, Windows')
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

    if os_name == 'Windows':
        write_iso_windows(iso_path, target)
    else:
        write_iso_unix(iso_path, target)

if __name__ == '__main__':
    main()

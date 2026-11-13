#!/usr/bin/env python3
"""Prepare the four accepted PSP installers and manifests without publishing."""
from datetime import datetime
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from zoneinfo import ZoneInfo


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def files(path):
    return sorted(p for p in path.rglob('*') if p.is_file() and not p.is_symlink()
                  and p.name != '.DS_Store' and '__pycache__' not in p.parts)


def checksum_file(directory):
    (directory / 'SHA256SUMS').write_text(''.join(
        digest(path) + '  ' + path.relative_to(directory).as_posix() + '\n'
        for path in files(directory) if path.name != 'SHA256SUMS'))


def make_zip(target, members):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for path, name in members:
            archive.write(path, name)
    with zipfile.ZipFile(target) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('ZIP CRC failed: ' + str(target))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'v\d{4}\.\d{2}\.\d{2}', args.tag):
        parser.error('Expected a dated tag such as v2026.11.13')
    repo = Path(__file__).resolve().parents[1]
    workspace = repo.parent
    output = repo / '_release_assets' / args.tag
    if output.exists():
        parser.error('Output exists; inspect it before preparing another release')
    current = json.loads((workspace / 'CURRENT_PROJECTS.json').read_text())
    selected = sorted((p for p in current['projects'] if p['status'] == 'official'),
                      key=lambda p: p['original_index'])
    if [p['original_index'] for p in selected] != [1, 2, 3, 4]:
        parser.error('Expected exactly the four accepted projects 1-4')
    filenames = {'forest-temple': 'ForestTemple_PSP-build11.zip', 'elf-story': 'ElfStory_PSP-1.0.1.zip',
                 'flipside': 'Flipside_PSP-1.0.3.zip', 'badminton': 'Badminton2_PSP-1.0.18.zip'}
    output.mkdir()
    manifest = {'schema_version': 1, 'release': args.tag, 'status': 'official',
                'created_at': datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),
                'repository': 'https://github.com/Sumo1311/4399-on-PSP',
                'license': 'MIT for original contributions only; see LICENSE_SCOPE.md and THIRD_PARTY_NOTICES.md',
                'games': {}, 'assets': {},
                'validation': {'new_real_psp_tests': False, 'new_two_real_psp_wlan_tests': False}}
    with tempfile.TemporaryDirectory(prefix='psp-official-release-') as temporary:
        staging = Path(temporary)
        install_root = staging / 'install'
        for project in selected:
            source = workspace / project['install_directory']
            game = source.name
            package = install_root / 'PSP/GAME' / game
            before = {p.relative_to(source).as_posix(): digest(p) for p in files(source)}
            if before['EBOOT.PBP'] != project['eboot_sha256']:
                raise RuntimeError('Official program identity mismatch: ' + game)
            shutil.copytree(source, package)
            (package / 'RELINKING.md').write_bytes((repo / 'RELINKING.md').read_bytes())
            (package / 'OFFICIAL_RELEASE.txt').write_text(
                project['title'] + ' · ' + project['version'] + '\n正式发行：' + args.tag + '\n'
                '程序与游戏资源保持定版身份；本次发行包含 2026-11-11 性能优化。\n')
            (package / 'INSTALL.txt').write_text(
                project['title'] + '\n\n将完整游戏目录复制到 PSP/GAME/' + game + '。\n'
                'PSP 系统需支持自制程序；PPSSPP 可打开同目录 EBOOT.PBP。\n'
                '更新前保留个人存档，并完整替换程序和素材。联机的两台 PSP 必须使用相同版本。\n'
                '四款正式版、校验和及构建材料：\nhttps://github.com/Sumo1311/4399-on-PSP/releases/tag/' + args.tag + '\n')
            checksum_file(package)
            after = {p.relative_to(package).as_posix(): digest(p) for p in files(package)}
            allowed = {'RELINKING.md', 'OFFICIAL_RELEASE.txt', 'INSTALL.txt', 'SHA256SUMS'}
            if set(before) != set(after) or any(before[name] != after[name] for name in before if name not in allowed):
                raise RuntimeError('Unexpected runtime or resource change: ' + game)
            archive = output / filenames[project['id']]
            make_zip(archive, [(p, p.relative_to(install_root).as_posix()) for p in files(package)])
            manifest['games'][game] = {'title': project['title'], 'version': project['version'],
                'eboot_sha256': project['eboot_sha256'], 'program_sha256': project['program_sha256'],
                'runtime_and_game_assets_unchanged': True,
                'files': {name: {'bytes': (package / name).stat().st_size, 'sha256': value} for name, value in after.items()}}
            print('Prepared', archive.name, flush=True)
        instructions = install_root / '安装说明.txt'
        instructions.write_text('PSP 上的 4399 · 四款正式版 ' + args.tag + '\n\n'
            '将 PSP 文件夹合并复制到记忆棒根目录。路径应为 PSP/GAME/游戏目录/EBOOT.PBP。\n'
            '更新前备份并保留个人存档。两台 PSP 联机需使用完全相同的游戏版本。\n'
            '本包仅包含森林神殿、精灵物语、Flipside 和火柴人羽毛球 2。\n')
        combined = output / ('Flash-to-PSP-Collection-' + args.tag[1:] + '.zip')
        make_zip(combined, [(p, p.relative_to(install_root).as_posix()) for p in files(install_root)])
        with zipfile.ZipFile(combined) as archive:
            if len([name for name in archive.namelist() if name.endswith('/EBOOT.PBP')]) != 4:
                raise RuntimeError('Expected exactly four programs in the collection')
        print('Prepared', combined.name, flush=True)

        materials = staging / 'materials'
        original = repo / '_release_assets/PSP4399-Performance-Build-Materials-2026.11.11.zip'
        with zipfile.ZipFile(original) as archive:
            for member in archive.infolist():
                name = PurePosixPath(member.filename)
                if name.is_absolute() or '..' in name.parts or '\\' in member.filename:
                    raise RuntimeError('Invalid build-material member')
            archive.extractall(materials)
        bundle = materials / 'Performance-Build-Materials'
        (bundle / 'README.txt').write_text('四款正式版 ' + args.tag + ' 的构建与重新链接材料。\n'
            '包含对象文件、链接配置、EBOOT 模板、修改源码、线程库对应源码及许可证。\n'
            '使用同一发行中的安装包提供外部游戏资源。\n'
            '重新链接方法见 RELINKING.md；本归档不包含完整游戏工程源码。\n')
        (bundle / 'RELINKING.md').write_bytes((repo / 'RELINKING.md').read_bytes())
        relink_results = []
        sdk = workspace / 'psp_optimization_pipeline/.tools/pspdev'
        for project in selected:
            game = Path(project['install_directory']).name
            rebuilt = staging / 'relinked' / game / 'EBOOT.PBP'
            subprocess.run([sys.executable, str(bundle / 'relink.py'), str(bundle), game,
                            '--sdk', str(sdk), '--output', str(rebuilt), '--check-identical'], check=True)
            if digest(rebuilt) != project['eboot_sha256']:
                raise RuntimeError('Relinked PBP differs from the official program: ' + game)
            relink_results.append({'game': game, 'eboot_sha256': digest(rebuilt), 'status': 'byte_identical'})
        (bundle / 'relink-verification.json').write_text(json.dumps(relink_results, ensure_ascii=False, indent=2) + '\n')
        archive = output / ('Relink-Materials-' + args.tag[1:] + '.zip')
        make_zip(archive, [(p, p.relative_to(materials).as_posix()) for p in files(materials)])
        manifest['validation'].update(zip_crc='passed', runtime_identity='passed', relinking=relink_results)
        print('Prepared and verified', archive.name, flush=True)
    for path in sorted(output.glob('*.zip')):
        manifest['assets'][path.name] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
    public_manifest = output / 'Release_Manifest.json'
    public_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    shutil.copyfile(public_manifest, repo / 'manifests' / ('release-' + args.tag[1:] + '.json'))
    checksum_file(output)
    print(json.dumps({'status': 'prepared', 'directory': str(output), 'assets': len(files(output))}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()

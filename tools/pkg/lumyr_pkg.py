#!/usr/bin/env python3
"""
Lumyr 包管理器

类似 npm/cargo/go mod 的包管理工具，支持依赖管理、版本解析、包发布。

用法:
  lumyr_pkg init [name]           初始化新项目
  lumyr_pkg add <pkg> [version]   添加依赖
  lumyr_pkg remove <pkg>          移除依赖
  lumyr_pkg install                安装所有依赖
  lumyr_pkg list                   列出已安装依赖
  lumyr_pkg publish                发布包到注册表
  lumyr_pkg search <query>        搜索包
  lumyr_pkg info <pkg>            查看包信息
  lumyr_pkg update [pkg]          更新依赖
  lumyr_pkg run <script>          运行脚本
  lumyr_pkg --help                 显示帮助
"""
import os
import sys
import json
import argparse
import hashlib
import shutil
import re
from pathlib import Path
from datetime import datetime

# ============== 配置 ==============

CONFIG_FILE = 'lumyr.json'
MODULES_DIR = 'lumyr_modules'
REGISTRY_DIR = Path.home() / '.lumyr' / 'registry'
CACHE_DIR = Path.home() / '.lumyr' / 'cache'

# ============== 语义化版本 ==============

class SemVer:
    """语义化版本号解析与比较"""
    def __init__(self, version_str):
        self.raw = version_str.strip()
        self.major = 0
        self.minor = 0
        self.patch = 0
        self.prerelease = ''
        self.build = ''
        self._parse()

    def _parse(self):
        v = self.raw
        # 移除前导 v
        if v.startswith('v'):
            v = v[1:]
        # 分离构建元数据
        if '+' in v:
            v, self.build = v.split('+', 1)
        # 分离预发布
        if '-' in v:
            v, self.prerelease = v.split('-', 1)
        # 解析主版本.次版本.修订号
        parts = v.split('.')
        if len(parts) >= 1:
            self.major = int(parts[0]) if parts[0].isdigit() else 0
        if len(parts) >= 2:
            self.minor = int(parts[1]) if parts[1].isdigit() else 0
        if len(parts) >= 3:
            self.patch = int(parts[2]) if parts[2].isdigit() else 0

    def tuple(self):
        return (self.major, self.minor, self.patch)

    def __eq__(self, other):
        return self.tuple() == other.tuple() and self.prerelease == other.prerelease

    def __lt__(self, other):
        if self.tuple() != other.tuple():
            return self.tuple() < other.tuple()
        # 有预发布版本的优先级低于正式版本
        if self.prerelease and not other.prerelease:
            return True
        if not self.prerelease and other.prerelease:
            return False
        if self.prerelease and other.prerelease:
            return self.prerelease < other.prerelease
        return False

    def __le__(self, other):
        return self == other or self < other

    def __gt__(self, other):
        return not self <= other

    def __ge__(self, other):
        return not self < other

    def __str__(self):
        result = f"{self.major}.{self.minor}.{self.patch}"
        if self.prerelease:
            result += f"-{self.prerelease}"
        if self.build:
            result += f"+{self.build}"
        return result

    def __repr__(self):
        return f"SemVer('{self}')"


class VersionConstraint:
    """版本约束解析（^, ~, >=, <=, >, <, =, *）"""
    def __init__(self, constraint_str):
        self.raw = constraint_str.strip()
        self.constraints = []
        self._parse()

    def _parse(self):
        c = self.raw
        if c == '*' or c == 'latest' or c == '':
            self.constraints.append(('>=', SemVer('0.0.0')))
            return

        # 处理 ^ 约束（兼容更新）
        if c.startswith('^'):
            v = SemVer(c[1:])
            self.constraints.append(('>=', v))
            if v.major > 0:
                self.constraints.append(('<', SemVer(f"{v.major+1}.0.0")))
            elif v.minor > 0:
                self.constraints.append(('<', SemVer(f"0.{v.minor+1}.0")))
            else:
                self.constraints.append(('<', SemVer(f"0.0.{v.patch+1}")))
            return

        # 处理 ~ 约束（保守更新）
        if c.startswith('~'):
            v = SemVer(c[1:])
            self.constraints.append(('>=', v))
            self.constraints.append(('<', SemVer(f"{v.major}.{v.minor+1}.0")))
            return

        # 处理比较运算符
        match = re.match(r'^(>=|<=|>|<|=)\s*(.+)$', c)
        if match:
            op = match.group(1)
            v = SemVer(match.group(2))
            self.constraints.append((op, v))
            return

        # 精确版本
        v = SemVer(c)
        self.constraints.append(('=', v))

    def satisfies(self, version):
        """检查版本是否满足约束"""
        v = version if isinstance(version, SemVer) else SemVer(version)
        for op, constraint in self.constraints:
            if op == '>=' and not (v >= constraint):
                return False
            if op == '<=' and not (v <= constraint):
                return False
            if op == '>' and not (v > constraint):
                return False
            if op == '<' and not (v < constraint):
                return False
            if op == '=' and not (v == constraint):
                return False
        return True

    def __str__(self):
        return self.raw


# ============== 包注册表 ==============

class PackageRegistry:
    """包注册表（本地文件系统实现）"""

    def __init__(self):
        self.registry_dir = REGISTRY_DIR
        self.registry_dir.mkdir(parents=True, exist_ok=True)

    def get_package_path(self, name, version):
        return self.registry_dir / name / str(version)

    def publish(self, name, version, source_dir, metadata):
        """发布包到注册表"""
        pkg_path = self.get_package_path(name, version)
        if pkg_path.exists():
            return False, f"Package {name}@{version} already exists"

        pkg_path.mkdir(parents=True)

        # 复制源码
        for item in Path(source_dir).iterdir():
            if item.name in (MODULES_DIR, '.git', '__pycache__'):
                continue
            dest = pkg_path / item.name
            if item.is_dir():
                shutil.copytree(item, dest)
            else:
                shutil.copy2(item, dest)

        # 保存元数据
        metadata['published_at'] = datetime.now().isoformat()
        with open(pkg_path / 'package.json', 'w', encoding='utf-8') as f:
            json.dump(metadata, f, indent=2, ensure_ascii=False)

        return True, f"Published {name}@{version}"

    def get_versions(self, name):
        """获取包的所有版本"""
        pkg_dir = self.registry_dir / name
        if not pkg_dir.exists():
            return []
        versions = []
        for d in pkg_dir.iterdir():
            if d.is_dir():
                try:
                    versions.append(SemVer(d.name))
                except:
                    pass
        return sorted(versions)

    def get_latest(self, name):
        """获取最新版本"""
        versions = self.get_versions(name)
        return versions[-1] if versions else None

    def resolve(self, name, constraint_str):
        """解析版本约束，返回满足约束的最新版本"""
        versions = self.get_versions(name)
        if not versions:
            return None
        constraint = VersionConstraint(constraint_str)
        satisfying = [v for v in versions if constraint.satisfies(v)]
        return satisfying[-1] if satisfying else None

    def install(self, name, version, dest_dir):
        """安装包到目标目录"""
        pkg_path = self.get_package_path(name, version)
        if not pkg_path.exists():
            return False, f"Package {name}@{version} not found in registry"

        dest = Path(dest_dir) / name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(pkg_path, dest)

        # 移除 package.json（运行时不需要）
        pkg_json = dest / 'package.json'
        if pkg_json.exists():
            pkg_json.unlink()

        return True, f"Installed {name}@{version}"

    def search(self, query):
        """搜索包"""
        results = []
        if not self.registry_dir.exists():
            return results
        for name_dir in self.registry_dir.iterdir():
            if name_dir.is_dir() and query.lower() in name_dir.name.lower():
                versions = self.get_versions(name_dir.name)
                results.append({
                    'name': name_dir.name,
                    'versions': [str(v) for v in versions],
                    'latest': str(versions[-1]) if versions else None
                })
        return results

    def get_info(self, name):
        """获取包信息"""
        versions = self.get_versions(name)
        if not versions:
            return None
        latest = versions[-1]
        pkg_path = self.get_package_path(name, latest)
        metadata = {}
        pkg_json = pkg_path / 'package.json'
        if pkg_json.exists():
            with open(pkg_json, 'r', encoding='utf-8') as f:
                metadata = json.load(f)
        return {
            'name': name,
            'latest': str(latest),
            'versions': [str(v) for v in versions],
            'metadata': metadata
        }


# ============== 项目配置 ==============

class ProjectConfig:
    """项目配置（lumyr.json）"""

    def __init__(self, project_dir='.'):
        self.project_dir = Path(project_dir)
        self.config_path = self.project_dir / CONFIG_FILE
        self.config = {}
        self.load()

    def load(self):
        if self.config_path.exists():
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config = json.load(f)
        else:
            self.config = {
                'name': '',
                'version': '0.1.0',
                'description': '',
                'main': 'main.lm',
                'scripts': {},
                'dependencies': {},
                'devDependencies': {}
            }

    def save(self):
        with open(self.config_path, 'w', encoding='utf-8') as f:
            json.dump(self.config, f, indent=2, ensure_ascii=False)

    def add_dependency(self, name, version, dev=False):
        key = 'devDependencies' if dev else 'dependencies'
        if key not in self.config:
            self.config[key] = {}
        self.config[key][name] = version
        self.save()

    def remove_dependency(self, name, dev=False):
        key = 'devDependencies' if dev else 'dependencies'
        if key in self.config and name in self.config[key]:
            del self.config[key][name]
            self.save()
            return True
        return False

    def get_dependencies(self, dev=True):
        deps = dict(self.config.get('dependencies', {}))
        if dev:
            deps.update(self.config.get('devDependencies', {}))
        return deps


# ============== 命令实现 ==============

def cmd_init(args):
    """初始化新项目"""
    name = args.name or Path.cwd().name
    config = ProjectConfig('.')
    config.config['name'] = name
    config.config['version'] = '0.1.0'
    config.config['description'] = ''
    config.config['main'] = 'main.lm'
    config.config['scripts'] = {
        'start': 'lumyr main.lm',
        'build': 'lumyr -c main.lm -o main',
        'test': 'lumyr tests/'
    }
    config.config['dependencies'] = {}
    config.config['devDependencies'] = {}
    config.save()

    # 创建主文件
    main_file = Path('main.lm')
    if not main_file.exists():
        main_file.write_text(f'// {name}\nprint("Hello, {name}!");\n', encoding='utf-8')

    # 创建 .gitignore
    gitignore = Path('.gitignore')
    if not gitignore.exists():
        gitignore.write_text(f'{MODULES_DIR}/\nbuild/\n*.exe\n*.o\n', encoding='utf-8')

    print(f"Initialized project: {name}")
    print(f"  Config: {CONFIG_FILE}")
    print(f"  Main: main.lm")
    print(f"  Modules dir: {MODULES_DIR}/")


def cmd_add(args):
    """添加依赖"""
    config = ProjectConfig('.')
    registry = PackageRegistry()

    name = args.package
    version = args.version or 'latest'

    # 解析版本
    if version == 'latest':
        latest = registry.get_latest(name)
        if latest:
            version = f"^{latest.major}.{latest.minor}.{latest.patch}"
        else:
            version = "^0.1.0"

    config.add_dependency(name, version, dev=args.dev)
    print(f"Added dependency: {name}@{version}")

    # 自动安装
    if not args.no_install:
        cmd_install(argparse.Namespace(no_save=False))


def cmd_remove(args):
    """移除依赖"""
    config = ProjectConfig('.')
    name = args.package

    removed = config.remove_dependency(name, dev=False)
    if not removed:
        removed = config.remove_dependency(name, dev=True)

    if removed:
        print(f"Removed dependency: {name}")
        # 移除已安装的包
        pkg_dir = Path(MODULES_DIR) / name
        if pkg_dir.exists():
            shutil.rmtree(pkg_dir)
            print(f"  Removed from {MODULES_DIR}/")
    else:
        print(f"Dependency not found: {name}")


def cmd_install(args):
    """安装所有依赖"""
    config = ProjectConfig('.')
    registry = PackageRegistry()
    modules_dir = Path(MODULES_DIR)
    modules_dir.mkdir(exist_ok=True)

    deps = config.get_dependencies(dev=not args.production)
    if not deps:
        print("No dependencies to install.")
        return

    print(f"Installing {len(deps)} dependencies...")
    installed = 0
    failed = []

    for name, constraint_str in deps.items():
        version = registry.resolve(name, constraint_str)
        if not version:
            # 尝试获取所有版本
            versions = registry.get_versions(name)
            if versions:
                print(f"  {name}: no version satisfies {constraint_str} (available: {', '.join(str(v) for v in versions)})")
            else:
                print(f"  {name}: not found in registry")
            failed.append(name)
            continue

        success, msg = registry.install(name, version, modules_dir)
        if success:
            print(f"  {name}@{version} installed")
            installed += 1
        else:
            print(f"  {name}: {msg}")
            failed.append(name)

    # 保存 lock 文件
    lock_file = Path('lumyr-lock.json')
    lock_data = {
        'name': config.config.get('name', ''),
        'version': config.config.get('version', ''),
        'lockfile_version': 1,
        'dependencies': {}
    }
    for name, constraint_str in deps.items():
        version = registry.resolve(name, constraint_str)
        if version:
            lock_data['dependencies'][name] = {
                'version': str(version),
                'constraint': constraint_str,
                'integrity': hashlib.sha256(str(version).encode()).hexdigest()
            }
    with open(lock_file, 'w', encoding='utf-8') as f:
        json.dump(lock_data, f, indent=2, ensure_ascii=False)

    print(f"\nInstalled: {installed}, Failed: {len(failed)}")
    if failed:
        print(f"Failed packages: {', '.join(failed)}")


def cmd_list(args):
    """列出已安装依赖"""
    config = ProjectConfig('.')
    modules_dir = Path(MODULES_DIR)

    deps = config.get_dependencies()
    if not deps:
        print("No dependencies.")
        return

    print("Dependencies:")
    for name, constraint in sorted(deps.items()):
        installed = ''
        pkg_dir = modules_dir / name
        if pkg_dir.exists():
            pkg_json = pkg_dir / 'package.json'
            if pkg_json.exists():
                with open(pkg_json, 'r', encoding='utf-8') as f:
                    pkg_config = json.load(f)
                    installed = f"@{pkg_config.get('version', '?')}"
            else:
                installed = "@installed"
        else:
            installed = "@not installed"
        print(f"  {name} {constraint} {installed}")


def cmd_publish(args):
    """发布包到注册表"""
    config = ProjectConfig('.')
    registry = PackageRegistry()

    name = config.config.get('name', '')
    version = config.config.get('version', '0.1.0')

    if not name:
        print("Error: package name not set in lumyr.json")
        return

    metadata = {
        'name': name,
        'version': version,
        'description': config.config.get('description', ''),
        'main': config.config.get('main', 'main.lm'),
        'dependencies': config.config.get('dependencies', {}),
    }

    success, msg = registry.publish(name, version, '.', metadata)
    if success:
        print(msg)
        print(f"  Registry: {registry.registry_dir}")
    else:
        print(f"Error: {msg}")


def cmd_search(args):
    """搜索包"""
    registry = PackageRegistry()
    results = registry.search(args.query)

    if not results:
        print(f"No packages found matching '{args.query}'.")
        return

    print(f"Found {len(results)} package(s):")
    for pkg in results:
        print(f"  {pkg['name']}@{pkg['latest']}")
        print(f"    Versions: {', '.join(pkg['versions'])}")


def cmd_info(args):
    """查看包信息"""
    registry = PackageRegistry()
    info = registry.get_info(args.package)

    if not info:
        print(f"Package not found: {args.package}")
        return

    print(f"Package: {info['name']}")
    print(f"Latest: {info['latest']}")
    print(f"Versions: {', '.join(info['versions'])}")
    if info['metadata']:
        print(f"Description: {info['metadata'].get('description', 'N/A')}")
        print(f"Main: {info['metadata'].get('main', 'N/A')}")
        if info['metadata'].get('dependencies'):
            print("Dependencies:")
            for name, ver in info['metadata']['dependencies'].items():
                print(f"  {name}@{ver}")


def cmd_update(args):
    """更新依赖"""
    config = ProjectConfig('.')
    registry = PackageRegistry()

    if args.package:
        packages = [args.package]
    else:
        packages = list(config.get_dependencies().keys())

    updated = 0
    for name in packages:
        constraint = config.get_dependencies().get(name, 'latest')
        current = registry.resolve(name, constraint)
        latest = registry.get_latest(name)
        if current and latest and latest > current:
            print(f"  {name}: {current} -> {latest}")
            # 更新约束为最新版本的 ^ 约束
            new_constraint = f"^{latest.major}.{latest.minor}.{latest.patch}"
            config.add_dependency(name, new_constraint)
            updated += 1
        elif current:
            print(f"  {name}: already up to date ({current})")

    if updated > 0:
        print(f"\nUpdated {updated} package(s). Run 'lumyr_pkg install' to install.")
    else:
        print("All packages are up to date.")


def cmd_run(args):
    """运行脚本"""
    config = ProjectConfig('.')
    scripts = config.config.get('scripts', {})

    if args.script not in scripts:
        print(f"Script not found: {args.script}")
        print("Available scripts:")
        for name in scripts:
            print(f"  {name}")
        return

    cmd = scripts[args.script]
    print(f"Running: {cmd}")
    os.system(cmd)


# ============== 主入口 ==============

def main():
    parser = argparse.ArgumentParser(
        description='Lumyr Package Manager',
        usage='lumyr_pkg <command> [options]'
    )
    subparsers = parser.add_subparsers(dest='command', help='Available commands')

    # init
    p_init = subparsers.add_parser('init', help='Initialize a new project')
    p_init.add_argument('name', nargs='?', help='Package name')

    # add
    p_add = subparsers.add_parser('add', help='Add a dependency')
    p_add.add_argument('package', help='Package name')
    p_add.add_argument('version', nargs='?', help='Version or constraint (default: latest)')
    p_add.add_argument('--dev', action='store_true', help='Add as dev dependency')
    p_add.add_argument('--no-install', action='store_true', help='Do not install after adding')

    # remove
    p_remove = subparsers.add_parser('remove', help='Remove a dependency')
    p_remove.add_argument('package', help='Package name')

    # install
    p_install = subparsers.add_parser('install', help='Install all dependencies')
    p_install.add_argument('--production', action='store_true', help='Skip dev dependencies')

    # list
    subparsers.add_parser('list', help='List installed dependencies')

    # publish
    subparsers.add_parser('publish', help='Publish package to registry')

    # search
    p_search = subparsers.add_parser('search', help='Search packages')
    p_search.add_argument('query', help='Search query')

    # info
    p_info = subparsers.add_parser('info', help='Show package info')
    p_info.add_argument('package', help='Package name')

    # update
    p_update = subparsers.add_parser('update', help='Update dependencies')
    p_update.add_argument('package', nargs='?', help='Package name (default: all)')

    # run
    p_run = subparsers.add_parser('run', help='Run a script')
    p_run.add_argument('script', help='Script name')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    commands = {
        'init': cmd_init,
        'add': cmd_add,
        'remove': cmd_remove,
        'install': cmd_install,
        'list': cmd_list,
        'publish': cmd_publish,
        'search': cmd_search,
        'info': cmd_info,
        'update': cmd_update,
        'run': cmd_run,
    }

    if args.command in commands:
        commands[args.command](args)
    else:
        parser.print_help()


if __name__ == '__main__':
    main()

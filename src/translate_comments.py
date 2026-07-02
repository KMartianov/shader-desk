#!/usr/bin/env python3
import json
import os
from pathlib import Path
import argparse

def load_dictionary(dict_path: str) -> dict:
    try:
        with open(dict_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"Error: Dictionary file '{dict_path}' not found.")
        exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON format in '{dict_path}': {e}")
        exit(1)

def process_file(file_path: Path, translation_map: dict, dry_run: bool) -> int:
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except UnicodeDecodeError:
        # Пропускаем бинарники или файлы с другой кодировкой
        return 0

    original_content = content
    replacements_made = 0

    # Проходим по словарю (сортируем по длине ключа по убыванию, чтобы "длинные" совпадения 
    # заменялись первыми и не ломались более короткими подстроками)
    sorted_keys = sorted(translation_map.keys(), key=len, reverse=True)
    
    for ru_text in sorted_keys:
        en_text = translation_map[ru_text]
        if ru_text in content:
            content = content.replace(ru_text, en_text)
            replacements_made += 1

    if replacements_made > 0:
        if not dry_run:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"  [UPDATED] {file_path} ({replacements_made} strings replaced)")
        else:
            print(f"  [DRY RUN] Would update: {file_path} ({replacements_made} strings)")
        return 1
    return 0

def main():
    parser = argparse.ArgumentParser(description="Automated comment translation tool.")
    parser.add_argument("--dict", default="translation_map.json", help="Path to JSON dictionary")
    parser.add_argument("--dry-run", action="store_true", help="Do not write changes to files")
    args = parser.parse_args()

    translation_map = load_dictionary(args.dict)
    print(f"Loaded dictionary with {len(translation_map)} entries.")

    # Папки, в которых будем искать код (по дереву из твоего вопроса)
    target_dirs = ["src", "plugins"]
    extensions = {".cpp", ".hpp", ".h", ".glsl"}
    
    files_processed = 0
    files_changed = 0

    for directory in target_dirs:
        dir_path = Path(directory)
        if not dir_path.exists():
            continue

        for filepath in dir_path.rglob("*"):
            if filepath.is_file() and filepath.suffix in extensions:
                files_processed += 1
                files_changed += process_file(filepath, translation_map, args.dry_run)

    print(f"\nDone! Processed {files_processed} files.")
    if args.dry_run:
        print(f"Dry run complete. {files_changed} files would be modified.")
    else:
        print(f"Successfully updated {files_changed} files.")

if __name__ == "__main__":
    main()
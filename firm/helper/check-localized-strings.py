# This script checks if all strings used as arguments to `localized_string` in the project files are present in the .lang files under the data/lang folder. If a string is used as an argument but not present in one of the files, log a warning. Omit the warning if the line has a comment that says "esper:untranslated"
import os
import re
import json
import sys

# ANSI escape codes for colors
ORANGE = "\033[38;5;208m"
WHITE = "\033[37m"
GREEN = "\033[32m"
RESET = "\033[0m"

def check_localized_strings(root_dir):
    print(f"Checking {root_dir}")
    lang_dir = os.path.join(root_dir, "data", "lang")
    if not os.path.isdir(lang_dir):
        print(f"Warning: Language directory not found: {lang_dir}")
        return

    lang_files = {}
    for filename in os.listdir(lang_dir):
        if filename.endswith(".lang"):
            filepath = os.path.join(lang_dir, filename)
            try:
                with open(filepath, "r", encoding="utf-8") as f:
                    # Load the JSON data
                    data = json.load(f)
                    # Extract the keys (the strings)
                    lang_files[filename] = (set(data.keys()), filepath)  # Added the full filepath here
            except json.JSONDecodeError as e:
                print(f"Error: Invalid JSON format in {filepath}: {e}")
                return
            except Exception as e:
                print(f"Error: Could not read {filepath}: {e}")
                return

    string_pattern = r"localized_string\s*\(\s*\"(.*?)\"\s*\)"
    untranslated_strings = {}  # Keep track of untranslated strings and the languages they're missing from
    any_untranslated = False  # Flag to track if any untranslated strings were found

    for root, _, files in os.walk(root_dir):
        for file in files:
            if file.endswith((".cpp", ".c", ".h", ".hpp", ".ino")):
                filepath = os.path.join(root, file)
                with open(filepath, "r", encoding="utf-8") as f:
                    for line_num, line in enumerate(f, 1):
                        if "esper:untranslated" in line:
                            continue
                        matches = re.findall(string_pattern, line)
                        for string in matches:
                            if string not in untranslated_strings:
                                untranslated_strings[string] = set()

                            missing_in_langs = []
                            for lang_file, (strings, lang_filepath) in lang_files.items():  # Get the filepath here
                                if string not in strings:
                                    missing_in_langs.append((lang_file, lang_filepath))  # Save lang_filepath too

                            if len(missing_in_langs) > 0:
                                any_untranslated = True  # Set the flag because we found at least one
                                for lang, lang_filepath in missing_in_langs:
                                    if lang not in untranslated_strings[string]:
                                        print(
                                            f"{ORANGE}Warning: Untranslated string {WHITE}'{string}'{ORANGE} missing in {lang_filepath}, first usage in {filepath}:{line_num}{RESET}",
                                            file=sys.stderr,  # Changed to stderr
                                        )
                                        untranslated_strings[string].add(lang)

    if not any_untranslated:
        print(f"{GREEN}No untranslated strings detected{RESET}")


check_localized_strings(os.path.abspath("."))

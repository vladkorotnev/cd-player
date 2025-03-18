#!/bin/bash

# Directory containing the folders to check
source_dir="./firm/.pio/build"
fs_dir="./firm/data"
# Target directory for copied files
target_dir="./webroot/fvu"

# Create the target directory if it doesn't exist
mkdir -p "$target_dir"

# Put all the files from the fs_dir into a tar.gz in target_dir
tar -czvf "$target_dir/esper-fs.efs" -C "$fs_dir" .

cp "./firm/version.txt" "$target_dir/esper.evr"

# Iterate over each entry in the source directory
for entry in "$source_dir"/*; do
    # Check if the entry is a directory
    if [ -d "$entry" ]; then
        # Define the path to firmware.bin inside the current directory
        firmware_file="$entry/firmware.bin"
        
        # Check if firmware.bin exists in the current directory
        if [ -f "$firmware_file" ]; then
            # Get the name of the directory (basename)
            folder_name=$(basename "$entry")
            
            # Copy, rename, and compress firmware.bin to the target directory
            gzip -c "$firmware_file" > "$target_dir/$folder_name.efu"
        fi
    fi
done

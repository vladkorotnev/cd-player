# PIO/esp32-arduino doesn't correctly calculate the binary size
# See thread for more info: https://esp32.com/viewtopic.php?f=19&t=43303&sid=b02e92b5c4d3e7c16f2c3b14b2d0edc6#p141312

Import("env")

import csv
from os.path import join, getsize

BINPATH = join("$BUILD_DIR", "${PROGNAME}.bin")

def post_program_action(source, target, env):
    found=False
    csvpath = env["PARTITIONS_TABLE_CSV"]
    print("Using partition table:", csvpath)
    with open(csvpath, 'r') as file:
        reader = csv.DictReader(file)
        for row in reader:
            if row['# Name'] == 'app0':
                app0_size = int(row['Size'].strip(), 16)
                found=True
                break

    if not found:
        raise Exception("Could not find app0 size")

    fp = target[0].get_path()
    sz = getsize(fp) #<- scons' getsize doesn't work when creating the file for the first time, huh!
    if sz >= app0_size:
        print("app0 is", app0_size, "bytes, but",fp,"is",sz,": too big!")
        raise Exception("resulting binary too big")
    else:
        print("app0 is", app0_size, "bytes, and",fp,"is",sz,": OK")


env.AddPostAction(BINPATH, post_program_action)
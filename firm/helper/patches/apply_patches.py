from os.path import join, isfile

Import("env")

FRAMEWORK_DIR = join(".pio", "libdeps", "esp32dev", "ArduinoAudioTools")
patchflag_path = join(FRAMEWORK_DIR, ".patching-done")

# patch file only if we didn't do it before
if not isfile(join(FRAMEWORK_DIR, ".patching-done")):
    original_file = join(FRAMEWORK_DIR, "src", "AudioTools", "CoreAudio", "AudioHttp", "HttpLineReader.h")
    print("Origin", original_file)
    patched_file = join("helper", "patches", "yield_in_linereader.patch")
    print("Patch", patched_file)
    assert isfile(original_file)
    assert isfile(patched_file)

    cmd = "patch %s %s" % (original_file, patched_file)
    print(cmd)
    env.Execute("patch %s %s" % (original_file, patched_file))
    # env.Execute("touch " + patchflag_path)


    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))
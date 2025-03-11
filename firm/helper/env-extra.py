from os.path import isfile
Import("env")
assert isfile(".env")
try:
  f = open(".env", "r")
  lines = f.readlines()
  envs = []
  for line in lines:
    line = line.strip()
    if len(line) > 0 and not line[0] == "#":
        envs.append("-D{}".format(line.strip()))
  env.Append(BUILD_FLAGS=envs)
except IOError:
  print("File .env not accessible",)
finally:
  f.close()
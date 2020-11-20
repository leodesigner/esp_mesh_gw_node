Import("env")

# please keep $SOURCE variable, it will be replaced with a path to firmware

# Generic
#env.Replace(
#    UPLOADER="executable or path to executable",
#    UPLOADCMD="$UPLOADER $UPLOADERFLAGS $SOURCE"
#)

# In-line command with arguments
#env.Replace(
#    UPLOADCMD="executable -arg1 -arg2 $SOURCE"
#)

# Python callback
def on_upload(source, target, env):
    print("-----> ",source[0], target[0])
    firmware_path = str(source[0])
    # do something
    env.Execute("scp " + firmware_path + "  pi@pi:~/tmp/.; ssh pi@pi esptool -p /dev/ttyUSB0 -b 250000 write_flash 0x0 tmp/firmware.bin ")

env.Replace(UPLOADCMD=on_upload)


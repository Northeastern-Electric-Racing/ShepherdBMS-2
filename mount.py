import subprocess as sub
import os

dvline = 0

#Function to get BUSID base on device name
def filter_name(x):
    if "CMSIS-DAP" in x:
        return True
    else:
        return False

#This opens terminal 1
os.system('start cmd.exe @cmd /c wsl -d ubuntu @cmd exit')

#These helps filter out the BUSID
output = str(sub.getoutput("usbipd wsl list"))
linedvs = output.splitlines() #Split lines of output base on device name

flt_busid = filter(filter_name, linedvs)
for x in flt_busid:
    dvline = x #Obtain the line of specific device

if dvline != 0:
    dvid = dvline.split(" ", 1)[0] #Keep only the BUSID in the line
    busid = str(dvid) #BUSID turn for list to single string

    #This opens Terminal 2
    #Terminal might need to be elevated to admin privileges for this
    sub.check_call('cmd /k usbipd wsl attach --distribution=ubuntu --busid=' + busid)
else:
    sub.check_call('cmd /k @echo "Probe not found"')
#!/usr/bin/env python
''' DCDarkNet badge programmer script!

    This script connects to a badge micro through an ST-Link device.
    It reads the UID and checks if the keys have already been programmed.

    If they keys have not been programmed, the UID is used to check the key
    DB file to see if the device has previously been programmed. If the device
    has been previously programmed, the same keys are flashed back on it.

    If the device had not been previously programmed, a new key file is
    selected and flashed onto the device. Once the keys have been verified
    written, the key DB file is updated and the key file is moved to the 
    used/ directory.

    TODO: If a main flash file is provided, it is also programmed into the device.
'''

import os
import csv
import re
import shutil
import time
import math
from openocd.flashProgrammer import flashProgrammer

KEY_DIR = '../../BadgeGen/Debug/keys'
USED_KEY_DIR = KEY_DIR + '/used'
KEY_DB_FILE = KEY_DIR + '/used_keys.csv'

MAIN_FLASH_ADDR = 0x8000000
FLASH_BASE = 0x8000000
KEY_FLASH_OFFSET = 0xffd4
SECTOR_SIZE = 0x400

def initialSetup():
    """ Make sure all required files and directories are present """

    if not os.path.exists(KEY_DIR):
        raise IOError('Key directory not found: ' + KEY_DIR)

    if not os.path.exists(USED_KEY_DIR):
        print('Used key dir not found, creating.')
        os.makedirs(USED_KEY_DIR)

    if not os.path.exists(KEY_DB_FILE):
        print('Used key database file not found, creating.')
        with open(KEY_DB_FILE, 'a') as dbfile:
            dbfile.write('uid,keyfile,timestamp\n')
            dbfile.close()


def readDB(filename):
    dbdict = {}
    with open(filename, 'r') as dbfile:
        reader = csv.reader(dbfile)

        next(reader, None)  # skip the header
        for row in reader:
            uid = row[0]
            dbdict[uid] = {'filename': row[1], 'timestamp': int(row[2])}

    return dbdict


def updateDB(filename, uid, key_file):
    
    # Move file to used dir
    key_filename = KEY_DIR + '/' + key_file
    shutil.move(key_filename, USED_KEY_DIR + '/')

    # Update db file
    with open(filename, 'a') as dbfile:
        dbfile.write('{},{},{}\n'.format(uid, key_file, int(time.time())))
        dbfile.close()


def readKeyFiles(keydir):
    keydir_list = os.listdir(keydir)
    keyfiles = []

    for filename in keydir_list:
        # Only add filenames that match the 4 hex digits regex
        if re.match(r'^[0-9A-Fa-f]{4}$', filename):
            keyfiles.append(filename)

    return keyfiles


def readUID(flasher):
    # Read device unique ID
    uid_bytes = flasher.readMem(0x1FFFF7E8, 12)
    uid = ''
    for byte in range(len(uid_bytes)):
        uid += '{:02X}'.format(uid_bytes[byte])
    
    return uid


def dcdcCheck(flasher):
    dcdc_bytes = flasher.readMem(FLASH_BASE + KEY_FLASH_OFFSET, 2)

    if dcdc_bytes[0] == 0xdc and dcdc_bytes[1] == 0xdc:
        return True
    else:
        return False


def roundToSectorSize(size):
    return int(math.ceil(float(size)/float(SECTOR_SIZE)) * SECTOR_SIZE)


def programKeyfile(flasher, key_filename):

    key_flash_size = os.path.getsize(key_filename)

    flasher.erase(FLASH_BASE + KEY_FLASH_OFFSET, roundToSectorSize(key_flash_size))

    flasher.flashFile(key_filename, KEY_FLASH_OFFSET)

    if flasher.verifyFile(key_filename, KEY_FLASH_OFFSET):
        return True
    else:
        return False


initialSetup()
dbdict = readDB(KEY_DB_FILE)
unused_keys = readKeyFiles(KEY_DIR)

try:
    flasher = flashProgrammer()

    if flasher.connected is True:
        print('Connected to openOCD')

        flasher._sendCmd('reset halt')  # Make sure the processor is stopped  

        uid = readUID(flasher)
        print('Device UID is ' + uid)
        
        # Check if key is already present in this device
        if dcdcCheck(flasher) is False:
            
            # Check database to see if we've already flashed this device
            if uid in dbdict:
                # Already used
                key_file = dbdict[uid]['filename']
                key_filename = USED_KEY_DIR + '/' + key_file
                if programKeyfile(flasher, key_filename) is True:
                    print('Key programmed successfully')
                else:
                    print('Error programming key')
                
            else:
                key_file = unused_keys.pop()
                key_filename = KEY_DIR + '/' + key_file
                if programKeyfile(flasher, key_filename) is True:
                    updateDB(KEY_DB_FILE, uid, key_file) 
                    print('Key programmed successfully')
                else:
                    print('Error programming key')

        else:
            print('Key already programmed')

    else:        
        raise IOError('Could not connect to flash programmer')
finally:
    # Make sure we kill the flasher process, otherwise openocd thread 
    # stays open in background
    if flasher:
        flasher.kill()

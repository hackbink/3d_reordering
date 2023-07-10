# reorderLib.py

import ctypes
import platform

# Load the C library
if platform.system() == 'Windows':
    _reorderLib = ctypes.CDLL('./reorderLib.dll')
else:
    _reorderLib = ctypes.CDLL('./reorderLib.so')

class reorderLibOperator:
    def __init__(self, max_nodes):
        global _reorderLib
        _reorderLib.initCache(max_nodes)
        return

    def getNumOfBlocks(self):
        global _reorderLib
        num_of_blocks = (ctypes.c_int*1)()
        p_num_of_blocks = ctypes.cast(num_of_blocks, ctypes.POINTER(ctypes.c_int))
        _reorderLib.getNumOfBlocks(p_num_of_blocks)
        return p_num_of_blocks[0]

    def getNumOfSgs(self):
        global _reorderLib
        num_of_sgs = (ctypes.c_int*1)()
        p_num_of_sgs = ctypes.cast(num_of_sgs, ctypes.POINTER(ctypes.c_int))
        _reorderLib.getNumOfSgs(p_num_of_sgs)
        return p_num_of_sgs[0]

    def getNumOfTracks(self):
        global _reorderLib
        num_of_tracks = (ctypes.c_int*1)()
        p_num_of_tracks = ctypes.cast(num_of_tracks, ctypes.POINTER(ctypes.c_int))
        _reorderLib.getNumOfTracks(p_num_of_tracks)
        return p_num_of_tracks[0]

    def convertLbaToPhy(self, lba):
        global _reorderLib
        sg=(ctypes.c_int*1)()
        pSg = ctypes.cast(sg, ctypes.POINTER(ctypes.c_int))
        track=(ctypes.c_int*1)()
        pTrack = ctypes.cast(track, ctypes.POINTER(ctypes.c_int))
        _reorderLib.getPhyFromLba(ctypes.c_int(lba), pSg, pTrack)
        return sg[0], track[0]

    def addLba(self, lba, num_of_blocks):
        global _reorderLib
        _reorderLib.addLba(ctypes.c_int(lba),1)
        return

    def getDistance(self, curr_lba, new_lba):
        global _reorderLib
        currSg=(ctypes.c_int*1)()
        pCurrSg = ctypes.cast(currSg, ctypes.POINTER(ctypes.c_int))
        currTrack=(ctypes.c_int*1)()
        pCurrTrack = ctypes.cast(currTrack, ctypes.POINTER(ctypes.c_int))
        _reorderLib.getPhyFromLba(ctypes.c_int(curr_lba), pCurrSg, pCurrTrack)

        newSg=(ctypes.c_int*1)()
        pNewSg = ctypes.cast(newSg, ctypes.POINTER(ctypes.c_int))
        newTrack=(ctypes.c_int*1)()
        pNewTrack = ctypes.cast(newTrack, ctypes.POINTER(ctypes.c_int))
        _reorderLib.getPhyFromLba(ctypes.c_int(new_lba), pNewSg, pNewTrack)

        distance=(ctypes.c_int*1)()
        pDistance = ctypes.cast(distance, ctypes.POINTER(ctypes.c_int))

        _reorderLib.getDistance(ctypes.c_int(currSg),ctypes.c_int(currTrack),ctypes.c_int(newSg),ctypes.c_int(newTrack), pDistance)
        return distance[0]

    def selectTarget(self):
        global _reorderLib
        targetLba=(ctypes.c_int*1)()
        pTargetLba = ctypes.cast(targetLba, ctypes.POINTER(ctypes.c_int))
        distance=(ctypes.c_int*1)()
        pDistance = ctypes.cast(distance, ctypes.POINTER(ctypes.c_int))
        _reorderLib.selectTargetLba(pTargetLba, pDistance)
        return targetLba[0], distance[0]

    def completeLba(self, lba):
        global _reorderLib
        _reorderLib.completeTarget(lba)
        return

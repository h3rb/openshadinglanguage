#!/usr/bin/python 

import os
import sys
from optparse import OptionParser

def runtest (command, outputs, cleanfiles="", failureok=0) :
    parser = OptionParser()
    parser.add_option("-p", "--path", help="add to executable path",
                      action="store", type="string", dest="path", default="")
    parser.add_option("-c", "--clean", help="clean up",
                      action="store_true", dest="clean", default=False)
    (options, args) = parser.parse_args()

    if options.clean :
        for out in outputs+cleanfiles :
            print "\tremoving " + out
            try :
                cmpresult = os.remove (out)
            except OSError :
                continue
        return (0)

    if options.path != "" :
        sys.path = [options.path] + sys.path
    #print "command = " + command

    cmdret = os.system (command)
    # print "cmdret = " + str(cmdret)

    if cmdret != 0 and failureok == 0 :
        print "FAIL CRASH?"
        return (1)

    err = 0
    for out in outputs :
        extension = os.path.splitext(out)[1]
        if extension == ".tif" or extension == ".exr" :
            # images -- use idiff
            cmpcommand = (os.path.join (os.environ['OPENIMAGEIOHOME'], "bin", "idiff")
                          + " " + out + " ref/" + out)
        else :
            # anything else, mainly text files
            cmpcommand = "diff " + out + " ref/" + out
        # print "cmpcommand = " + cmpcommand
        cmpresult = os.system (cmpcommand)
        if cmpresult == 0 :
            print "\tmatch " + out
        else :
            print "\tNO MATCH " + out
            err = 1

        if err == 0 :
            print "PASS"
        else :
            print "FAIL"

    # if everything passed, get rid of the temporary files
    if err == 0 :
        for out in outputs+cleanfiles :
            print "\tremoving " + out
            try :
                cmpresult = os.remove (out)
            except OSError :
                continue
            
    return (err)

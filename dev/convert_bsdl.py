#!/usr/bin/env python
"""Convert BSDL

Convert BSDL file to ftjrev compatible file

"""

import io
import sys
import getopt

import re

def get_attribute(f, ent, name):
    res = re.search("attribute\s+%s\s+of\s+%s\s+:\s+entity\s+is\s+(.+?);" % (name, ent), f, re.S|re.I)
    return res.group(1)

def main(argv=None):
    if argv is None:
        argv = sys.argv
    try:
        try:
            opts, args = getopt.getopt(argv[1:], "?i:o:", ["help", "input=", "output="])
        except getopt.error as msg:
             raise Usage(msg)
        # more code, unchanged  
    except Usage as err:
        print(err.msg, file=sys.stderr)
        print("for help use --help", file=sys.stderr)
        return 2
    
    ifn = ''
    ofn = ''
    
    # process options
    for o, a in opts:
        if o in ('-?', '--help'):
            print(__doc__)
            sys.exit(0)
        if o in ('-i', '--input'):
            ifn = a
            ofn = ifn + '.out'
        if o in ('-o', '--output'):
            ofn = a
    
    if ifn == '':
        print("Error: no input file specified")
        return 1
    if ofn == '':
        print("Error: no output file specified")
        return 1
    
    ifp = open(ifn, 'r')
    ofp = open(ofn, 'w')

    input_file = ifp.read()
    
    # name
    res = re.search("entity\s+(\S+)\s+is", input_file)
    name = res.group(1)
    print("name: %s" % name.replace("_", "-"))
    ofp.write("name %s\n" % name.replace("_", "-"))

    # idcode
    idcode = get_attribute(input_file, name, 'IDCODE_REGISTER')
    idcode = ''.join(idcode.split("\"")[1::2])
    print("idcode: %s" % idcode)

    # irsize
    irsize = get_attribute(input_file, name, 'INSTRUCTION_LENGTH')
    print("irsize: %s" % irsize)
    ofp.write("irsize %s\n" % irsize)

    # bssize
    bssize = get_attribute(input_file, name, 'BOUNDARY_LENGTH')
    print("bssize: %s" % bssize)
    ofp.write("bssize %s\n" % bssize)

    # instructions
    opcodes = get_attribute(input_file, name, 'INSTRUCTION_OPCODE')

    # sample
    res = re.search("SAMPLE\s+\((\d+)\)", opcodes)
    sample = res.group(1)
    print("sample: %s" % sample)
    ofp.write("sample %s\n" % sample)

    # extest
    res = re.search("EXTEST\s+\((\d+)\)", opcodes)
    extest = res.group(1)
    print("extest: %s" % extest)
    ofp.write("extest %s\n" % extest)

    # pins
    pins = get_attribute(input_file, name, 'BOUNDARY_REGISTER')

    # remove non-pin entries

    pins = re.sub("^\s+\"\s*(\d+).*BC_\d,\s+\*.+$", "", pins, flags=re.M)
    pins = re.sub("^\s*--.+$", "", pins, flags=re.M)

    # tristate outputs

    pins = re.sub("^\s+\"\s*(\d+).*BC_\d,\s+(\S+),\s+(output3|bidir),\s+X,\s+(\d+)+,\s+(\d).+$", "bsc[\g<1>] \g<2> Y\g<5> \g<4>", pins, flags=re.M|re.I)

    # inputs

    pins = re.sub("^\s+\"\s*(\d+).*BC_\d,\s+(\S+),\s+input,.+$", "bsc[\g<1>] \g<2> I", pins, flags=re.M|re.I)

    ofp.write(pins)
    ofp.write('\n')
    

if __name__ == "__main__":
    sys.exit(main())


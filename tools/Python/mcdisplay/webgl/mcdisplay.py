#!/usr/bin/env python
# -*- coding: utf-8 -*-
'''
mcdisplay webgl script.
'''
import sys
import os
import webbrowser
import logging
import argparse
import json
from datetime import datetime

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..'))

from mclib.pipetools import McrunPipeMan
from mclib.instrparser import TraceInstrParser, InstrObjectConstructor
from mclib.neutronparser import NeutronRayConstructor, TraceNeutronRayParser
from mclib.instrgeom import Vector3d, Transform, calcLargestBoundingVolumeWT

class SimpleWriter(object):
    ''' a minimal, django-omiting "glue file" writer tightly coupled to some comments in the file template.html '''
    def __init__(self, templatefile, campos, html_filename):
        self.template = templatefile
        self.campos = campos
        self.html_filename = html_filename
    
    def write(self):
        # load and modify
        template = open(self.template).read()
        lines = template.splitlines()
        for i in range(len(lines)):
            if 'INSERT_CAMPOS_HERE:' in lines[i]:
                campos_lidx = i
                lines[i] = '            x = %s, y = %s, z = %s; // line by SimpleWriter' % (str(self.campos.x), str(self.campos.y), str(self.campos.z))
        self.text = '\n'.join(lines)
        
        # write to disk
        try:
            f = open(self.html_filename, 'w')
            f.write(self.text)
        finally:
            f.close()

class DjangoWriter(object):
    ''' writes a django template from the instrument representation '''
    instrument = None
    text = ''
    templatefile = ''
    campos = None
    
    def __init__(self, instrument, templatefile, campos):
        self.instrument = instrument
        self.templatefile = templatefile
        self.campos = campos
        
        # django stuff
        from django.template import Context
        from django.template import Template
        self.Context = Context
        self.Template = Template
        from django.conf import settings
        settings.configure()
    
    def build(self):
        templ = open(self.templatefile).read()
        t = self.Template(templ)
        c = self.Context({'instrument': self.instrument, 
            'campos_x': self.campos.x, 'campos_y': self.campos.y, 'campos_z': self.campos.z,})
        self.text = t.render(c)
    
    def save(self, filename):
        ''' save template to disk '''
        try:
            f = open(filename, 'w')
            f.write(self.text)
        finally:
            f.close()

class McMicsplayReader(object):
    ''' High-level trace manager '''
    def __init__(self, args, n=None, dir=None, debug=False):
        ''' supported args: instr, inspect, default, instr_options '''
        if not os.path.exists(args.instr) or not os.path.splitext(args.instr)[1] not in ['instr', 'out']:
            print "Please supply a valid .instr or .out file."
            exit()
        
        # assemble command
        cmd = 'mcrun ' + args.instr + ' --trace'
        if n:
            cmd = cmd + ' --ncount=' + str(n)
        if dir:
            cmd = cmd + ' --dir=' + dir
        if args.instr_options:
            for o in args.instr_options:
                cmd = cmd + ' ' + o
        
        self.args = args
        self.n = n
        self.dir = dir
        self.debug = debug
        self.cmd = cmd
        self.pipeman = McrunPipeMan(cmd, inspect=args.inspect, send_enter=args.default)
    
    def read_instrument(self):
        ''' starts a pipe to mcrun given cmd, waits for instdef and reads, returning the parsed instrument '''
        self.pipeman.start_pipe()
        self.pipeman.join()
        
        instrdef = self.pipeman.read_instrdef()
        
        if self.debug:
            file_save(instrdef, 'instrdata')
        
        instrparser = TraceInstrParser(instrdef)
        instrbuilder = InstrObjectConstructor(instrparser.parsetree)
        instrument = instrbuilder.build_instr()
        
        return instrument
    
    def read_neutrons(self):
        ''' waits for pipeman object to finish, then read and parse neutron data '''
        print "reading neutron data..."
        neutrons = self.pipeman.read_neutrons()
        
        print self.pipeman.read_comments()
        
        if self.debug:
            file_save(neutrons, 'neutrondata')
        
        rayparser = TraceNeutronRayParser(neutrons)
        raybuilder = NeutronRayConstructor(rayparser.parsetree)
        rays = raybuilder.build_rays()
        
        return rays

def write_gluefile_html(instrument, html_filepath, first=None, last=None):
    ''' writes instrument definition to html/js '''
    # calculate campost by means of the component bounding boxes (mediated by drawcalls)
    drawcalls = []
    
    encountered_first = False
    encountered_last = False
    
    # sanity check of first / last:
    if first: 
        compnames = []
        for comp in instrument.components:
            compnames.append(comp.name)
        if not first in compnames:
            raise Exception('--first must be equal to a component name')
    
    # construct bounding volume given --first and --last
    for comp in instrument.components:
        # continue until we reach a component named first
        if first:
            if not encountered_first and comp.name == first:
                encountered_first = True
            if not encountered_first:
                continue
        # continue from encountering a component named last
        if last:
            if not encountered_last and comp.name == last:
                encountered_last = True
            if encountered_last:
                continue
        
        transform = Transform(comp.rot, comp.pos)
        for drawcall in comp.drawcalls:
            drawcalls.append((drawcall, transform))
    box = calcLargestBoundingVolumeWT(drawcalls)
    
    # create camera view coordinates given boudinng volume
    dx = box.x2 - box.x1
    dy = box.y2 - box.y1
    dz = box.z2 - box.z1
    
    x = -(box.x1 + max(dx, dz)/2)
    y = max(dx, dz)/2
    z = box.z1 + dz/2
    
    campos = Vector3d(x, y, z)
    
    # render html
    templatefile = os.path.join(os.path.dirname(__file__), "template.html")
    
    writer = SimpleWriter(templatefile, campos, html_filepath)
    writer.write()

def write_browse(instrument, raybundle, dir):
    ''' writes instrument definitions to html/ js '''
    # write mcdisplay.js
    mcd_filepath = os.path.join(os.path.dirname(__file__), 'mcdisplay.js')
    file_save(open(mcd_filepath).read(), os.path.join(dir, '_mcdisplay.js'))
    
    # write html
    html_filepath = os.path.join(dir, 'index.html')
    write_gluefile_html(instrument, html_filepath, first=args.first, last=args.last)
    
    # write instrument
    json_instr = 'MCDATA_instrdata = %s;' % json.dumps(instrument.jsonize(), indent=0)
    file_save(json_instr, os.path.join(dir, '_instr.js'))
    
    # write neutrons
    json_neutr = 'MCDATA_neutrondata = %s;' % json.dumps(raybundle.jsonize(), indent=0)
    file_save(json_neutr, os.path.join(dir, '_neutrons.js'))
    
    webbrowser.open_new_tab(html_filepath)

def get_datadirname(instrname):
    ''' returns an mcrun-like name-date-time string '''
    return "%s_%s" % (instrname, datetime.strftime(datetime.now(), "%Y%d%m_%H%M%S"))

def file_save(data, filename):
    ''' saves data for debug purposes '''
    f = open(filename, 'w')
    f.write(data)
    f.close()

def main(args):
    logging.basicConfig(level=logging.INFO)
    debug = False
    
    dir = get_datadirname(os.path.splitext(os.path.basename(args.instr))[0])

    reader = McMicsplayReader(args, n=100, dir=dir, debug=debug)

    instrument = reader.read_instrument()
    raybundle = reader.read_neutrons()
    
    write_browse(instrument, raybundle, dir)
    
    if debug:
        # this will enable template.html to load directly
        jsonized = json.dumps(instrument.jsonize(), indent=0)
        file_save(jsonized, 'jsonized.json')
    
    
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('instr', help='display this instrument file (.instr or .out)')
    parser.add_argument('--default', '-d', action='store_true', help='use instrument defaults (fast)')
    parser.add_argument('--inspect', help='display only neutrons reaching this component passed to mcrun')
    parser.add_argument('--first', help='zoom range first component')
    parser.add_argument('--last', help='zoom range last component')
    parser.add_argument('instr_options', nargs='*', help='simulation options and instrument params')
    
    args, unknown = parser.parse_known_args()
    # if --inspect --first or --last are given after instr, the remaining args become "unknown",
    # but we assume that they are instr_options
    if len(unknown)>0:
        args.instr_options = unknown
    
    main(args)
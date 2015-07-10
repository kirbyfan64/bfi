from fbuild.builders.platform import guess_platform
from fbuild.builders.cxx import guess_static
from fbuild.config import cxx as cxx_test
from fbuild.record import Record
from fbuild.path import Path
from fbuild.db import caches

from optparse import make_option
import os

class AsmJit(cxx_test.Test):
    asmjit_h = cxx_test.header_test('asmjit/asmjit.h')

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.external_libs.append('asmjit')

def pre_options(parser):
    group = parser.add_option_group('config options')
    group.add_options((
        make_option('--cxx', help='Use the given C++ compiler'),
        make_option('--asmjit-incdir', help='AsmJit include directory'),
        make_option('--asmjit-libdir', help='AsmJit library directory'),
        make_option('--use-color', help='Use colored output from the compiler',
                    action='store_true')
    ))

@caches
def configure(ctx):
    fl = ['-fdiagnostics-color'] if ctx.options.use_color else []
    id, ld = ['.'], []
    for t, l in ('inc', id), ('lib', ld):
        opt = getattr(ctx.options, 'asmjit_%sdir' % t)
        if opt: l.append(opt)
    cxx = guess_static(ctx, flags=fl, includes=id, libpaths=ld, platform_options=[
        ({'windows'}, {'flags': ['/EHsc']}),
        ({'posix'}, {'flags+': ['-std=c++11']})
    ])
    return Record(cxx=cxx)

def build(ctx):
    cxx = configure(ctx).cxx
    asmjit = AsmJit(cxx)
    if not AsmJit(cxx).asmjit_h: return
    bfi = cxx.build_exe('bfi', ['bfi.cpp'], external_libs=['asmjit'])
    ctx.install(bfi, 'bin')

bfi
===

An optimizing Brainf**k interpreter written using `AsmJit <https://github.com/kobalicek/asmjit/>`_.

Building
********

Get a copy of `Fbuild <https://github.com/felix-lang/fbuild>`_ FROM GIT (not the tarball!) and run::
   
   $ fbuild

To install:
   
   $ fbuild install

Running
*******

Usage::
   
   bfi <program> [<debug>=0]

Where ``<program>`` is the program you wish to run and ``<debug>`` is either 1 (print the generated assembly code) or 0 (do NOT print the generated assembly code).

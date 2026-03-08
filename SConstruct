# type: ignore

VariantDir('Temp', '.', duplicate=0)
env = Environment()

import os
output_dir = os.environ.get('SKLIBRETRO_OUTPUT_DIR', '#bin')

SConscript('Temp/SConscript', exports=['env', 'output_dir'])

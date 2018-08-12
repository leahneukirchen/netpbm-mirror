triangle.o: triangle.c importinc/netpbm/mallocvar.h \
 importinc/netpbm/pm_config.h utils.h fract.h boundaries.h framebuffer.h \
 importinc/netpbm/pam.h importinc/netpbm/pm_config.h \
 importinc/netpbm/pm.h importinc/netpbm/pnm.h importinc/netpbm/ppm.h \
 importinc/netpbm/pgm.h importinc/netpbm/pbm.h importinc/netpbm/ppmcmap.h \
 triangle.h limits_pamtris.h
framebuffer.o: framebuffer.c utils.h fract.h limits_pamtris.h \
 framebuffer.h importinc/netpbm/pam.h importinc/netpbm/pm_config.h \
 importinc/netpbm/pm.h importinc/netpbm/pnm.h importinc/netpbm/ppm.h \
 importinc/netpbm/pgm.h importinc/netpbm/pbm.h importinc/netpbm/ppmcmap.h
boundaries.o: boundaries.c importinc/netpbm/mallocvar.h \
 importinc/netpbm/pm_config.h utils.h fract.h boundaries.h
pamtris.o: pamtris.c importinc/netpbm/mallocvar.h \
 importinc/netpbm/pm_config.h importinc/netpbm/shhopt.h \
 importinc/netpbm/pam.h importinc/netpbm/pm_config.h \
 importinc/netpbm/pm.h importinc/netpbm/pnm.h importinc/netpbm/ppm.h \
 importinc/netpbm/pgm.h importinc/netpbm/pbm.h importinc/netpbm/ppmcmap.h \
 limits_pamtris.h framebuffer.h fract.h boundaries.h input.h
input.o: input.c importinc/netpbm/mallocvar.h \
 importinc/netpbm/pm_config.h limits_pamtris.h framebuffer.h fract.h \
 importinc/netpbm/pam.h importinc/netpbm/pm_config.h \
 importinc/netpbm/pm.h importinc/netpbm/pnm.h importinc/netpbm/ppm.h \
 importinc/netpbm/pgm.h importinc/netpbm/pbm.h importinc/netpbm/ppmcmap.h \
 triangle.h input.h
utils.o: utils.c fract.h utils.h
